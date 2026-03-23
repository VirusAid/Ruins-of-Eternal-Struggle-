#include "faction_economy.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "debug.h"
#include "flexbuffer_json-inl.h"
#include "flexbuffer_json.h"
#include "item.h"
#include "item_category.h"
#include "json.h"

static faction_economy_manager g_faction_economy_manager;
std::map<faction_id, faction_economy> faction_economy_manager::templates_;

faction_economy_manager &get_faction_economy_manager()
{
    return g_faction_economy_manager;
}

// ====== econ_category helpers ======

std::string econ_category_name( econ_category cat )
{
    switch( cat ) {
        case econ_category::food:
            return "food";
        case econ_category::medicine:
            return "medicine";
        case econ_category::weapons:
            return "weapons";
        case econ_category::ammo:
            return "ammo";
        case econ_category::tools:
            return "tools";
        case econ_category::clothing:
            return "clothing";
        case econ_category::electronics:
            return "electronics";
        case econ_category::luxury:
            return "luxury";
        default:
            return "unknown";
    }
}

econ_category econ_category_from_string( const std::string &s )
{
    if( s == "food" ) {
        return econ_category::food;
    } else if( s == "medicine" ) {
        return econ_category::medicine;
    } else if( s == "weapons" ) {
        return econ_category::weapons;
    } else if( s == "ammo" ) {
        return econ_category::ammo;
    } else if( s == "tools" ) {
        return econ_category::tools;
    } else if( s == "clothing" ) {
        return econ_category::clothing;
    } else if( s == "electronics" ) {
        return econ_category::electronics;
    } else if( s == "luxury" ) {
        return econ_category::luxury;
    }
    return econ_category::luxury; // default fallback
}

econ_category item_econ_category( const item &it )
{
    if( it.is_food() || it.is_food_container() ) {
        return econ_category::food;
    }
    if( it.is_medication() ) {
        return econ_category::medicine;
    }
    if( it.is_gun() || it.is_melee() ) {
        return econ_category::weapons;
    }
    if( it.is_ammo() || it.is_magazine() ) {
        return econ_category::ammo;
    }
    if( it.is_tool() ) {
        return econ_category::tools;
    }
    if( it.is_armor() ) {
        return econ_category::clothing;
    }
    // Check category name for electronics
    const item_category &cat = it.get_category_of_contents();
    if( cat.get_id().str().find( "elec" ) != std::string::npos ) {
        return econ_category::electronics;
    }
    return econ_category::luxury;
}

// ====== econ_category_data serialization ======

void econ_category_data::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "supply", supply );
    json.member( "demand", demand );
    json.member( "priority", priority );
    json.member( "price_mod", price_mod );
    json.end_object();
}

void econ_category_data::deserialize( const JsonObject &jo )
{
    jo.read( "supply", supply, false );
    jo.read( "demand", demand, false );
    jo.read( "priority", priority, false );
    jo.read( "price_mod", price_mod, false );
}

// ====== faction_economy ======

void faction_economy::init_defaults()
{
    for( int i = 0; i < static_cast<int>( econ_category::NUM_CATEGORIES ); i++ ) {
        econ_category cat = static_cast<econ_category>( i );
        if( categories.find( cat ) == categories.end() ) {
            econ_category_data data;
            data.supply = 100;
            data.demand = 100;
            data.priority = 1.0;
            data.price_mod = 1.0;
            categories[cat] = data;
        }
    }
}

double faction_economy::get_category_modifier( econ_category cat ) const
{
    auto it = categories.find( cat );
    if( it == categories.end() ) {
        return 1.0;
    }
    return it->second.price_mod * global_trade_mod;
}

double faction_economy::get_price_modifier( const item &it ) const
{
    econ_category cat = item_econ_category( it );
    return get_category_modifier( cat );
}

void faction_economy::record_purchase( const item &it, int amount )
{
    econ_category cat = item_econ_category( it );
    auto iter = categories.find( cat );
    if( iter == categories.end() ) {
        return;
    }
    // Player buys from faction: supply decreases, demand signal increases
    iter->second.supply = std::max( 0, iter->second.supply - amount * 5 );
    iter->second.demand = std::min( 500, iter->second.demand + amount * 3 );
    // Recalculate price modifier immediately
    double ratio = static_cast<double>( iter->second.demand ) /
                   std::max( 1.0, static_cast<double>( iter->second.supply ) );
    iter->second.price_mod = std::clamp( ratio * iter->second.priority, 0.3, 5.0 );
}

void faction_economy::record_sale( const item &it, int amount )
{
    econ_category cat = item_econ_category( it );
    auto iter = categories.find( cat );
    if( iter == categories.end() ) {
        return;
    }
    // Player sells to faction: supply increases, demand signal decreases
    iter->second.supply = std::min( 500, iter->second.supply + amount * 5 );
    iter->second.demand = std::max( 0, iter->second.demand - amount * 2 );
    // Recalculate price modifier immediately
    double ratio = static_cast<double>( iter->second.demand ) /
                   std::max( 1.0, static_cast<double>( iter->second.supply ) );
    iter->second.price_mod = std::clamp( ratio * iter->second.priority, 0.3, 5.0 );
}

void faction_economy::update( const time_point &now )
{
    if( last_update == calendar::turn_zero ) {
        last_update = now;
        return;
    }

    const int days_elapsed = to_days<int>( now - last_update );
    if( days_elapsed < 1 ) {
        return;
    }
    last_update = now;

    for( auto &[cat, data] : categories ) {
        // Drift supply toward 100 (equilibrium) by 10% per day
        for( int d = 0; d < days_elapsed; d++ ) {
            data.supply += static_cast<int>( ( 100 - data.supply ) * 0.1 );
            data.demand += static_cast<int>( ( 100 - data.demand ) * 0.1 );
        }
        data.supply = std::clamp( data.supply, 0, 500 );
        data.demand = std::clamp( data.demand, 0, 500 );

        // Recalculate price modifier
        double ratio = static_cast<double>( data.demand ) /
                       std::max( 1.0, static_cast<double>( data.supply ) );
        data.price_mod = std::clamp( ratio * data.priority, 0.3, 5.0 );
    }
}

void faction_economy::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "fac_id", fac_id );
    json.member( "last_update", last_update );
    json.member( "global_trade_mod", global_trade_mod );
    json.member( "categories" );
    json.start_object();
    for( const auto &[cat, data] : categories ) {
        json.member( econ_category_name( cat ) );
        data.serialize( json );
    }
    json.end_object();
    json.end_object();
}

void faction_economy::deserialize( const JsonObject &jo )
{
    jo.read( "fac_id", fac_id );
    jo.read( "last_update", last_update, false );
    jo.read( "global_trade_mod", global_trade_mod, false );
    if( jo.has_object( "categories" ) ) {
        JsonObject cats = jo.get_object( "categories" );
        for( int i = 0; i < static_cast<int>( econ_category::NUM_CATEGORIES ); i++ ) {
            econ_category cat = static_cast<econ_category>( i );
            std::string name = econ_category_name( cat );
            if( cats.has_object( name ) ) {
                JsonObject cat_jo = cats.get_object( name );
                categories[cat].deserialize( cat_jo );
            }
        }
    }
    init_defaults();
}

// ====== faction_economy_manager ======

faction_economy &faction_economy_manager::get( const faction_id &fid )
{
    auto it = economies_.find( fid );
    if( it != economies_.end() ) {
        return it->second;
    }
    // Check templates
    auto tmpl = templates_.find( fid );
    if( tmpl != templates_.end() ) {
        economies_[fid] = tmpl->second;
        economies_[fid].fac_id = fid;
        return economies_[fid];
    }
    // Create default
    faction_economy &econ = economies_[fid];
    econ.fac_id = fid;
    econ.init_defaults();
    return econ;
}

const faction_economy *faction_economy_manager::get_if_exists( const faction_id &fid ) const
{
    auto it = economies_.find( fid );
    if( it != economies_.end() ) {
        return &it->second;
    }
    return nullptr;
}

void faction_economy_manager::update_all( const time_point &now )
{
    for( auto &[fid, econ] : economies_ ) {
        econ.update( now );
    }
}

void faction_economy_manager::load( const JsonObject &jo )
{
    faction_id fid;
    jo.read( "faction_id", fid, true );

    faction_economy econ;
    econ.fac_id = fid;
    jo.read( "global_trade_mod", econ.global_trade_mod, false );

    if( jo.has_array( "priorities" ) ) {
        for( JsonObject pjo : jo.get_array( "priorities" ) ) {
            std::string cat_name = pjo.get_string( "category" );
            econ_category cat = econ_category_from_string( cat_name );
            econ_category_data data;
            pjo.read( "supply", data.supply, false );
            pjo.read( "demand", data.demand, false );
            pjo.read( "priority", data.priority, false );
            data.price_mod = static_cast<double>( data.demand ) /
                             std::max( 1.0, static_cast<double>( data.supply ) ) * data.priority;
            data.price_mod = std::clamp( data.price_mod, 0.3, 5.0 );
            econ.categories[cat] = data;
        }
    }
    econ.init_defaults();
    templates_[fid] = econ;
}

void faction_economy_manager::reset()
{
    templates_.clear();
}

void faction_economy_manager::serialize( JsonOut &json ) const
{
    json.start_array();
    for( const auto &[fid, econ] : economies_ ) {
        econ.serialize( json );
    }
    json.end_array();
}

void faction_economy_manager::deserialize( const JsonValue &jv )
{
    economies_.clear();
    for( JsonObject ejo : jv.get_array() ) {
        faction_economy econ;
        econ.deserialize( ejo );
        economies_[econ.fac_id] = econ;
    }
}
