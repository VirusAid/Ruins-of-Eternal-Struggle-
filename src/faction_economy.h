#pragma once
#ifndef CATA_SRC_FACTION_ECONOMY_H
#define CATA_SRC_FACTION_ECONOMY_H

#include <map>
#include <string>
#include <vector>

#include "calendar.h"
#include "type_id.h"

class faction;
class item;
class JsonObject;
class JsonOut;
class JsonValue;

// Resource categories tracked by the economy system
enum class econ_category : int {
    food = 0,
    medicine,
    weapons,
    ammo,
    tools,
    clothing,
    electronics,
    luxury,
    NUM_CATEGORIES
};

std::string econ_category_name( econ_category cat );
econ_category econ_category_from_string( const std::string &s );
econ_category item_econ_category( const item &it );

// Per-category supply/demand data for a faction
struct econ_category_data {
    int supply = 100;       // current stock level (abstract units)
    int demand = 100;       // current demand level (abstract units)
    double priority = 1.0;  // faction priority multiplier for this category
    double price_mod = 1.0; // cached: current price modifier (recalculated on tick)

    void serialize( JsonOut &json ) const;
    void deserialize( const JsonObject &jo );
};

// Economy state for a single faction
struct faction_economy {
    faction_id fac_id;

    // Per-category economy data
    std::map<econ_category, econ_category_data> categories;

    // Last time economy was updated
    time_point last_update = calendar::turn_zero;

    // Global trade modifier (faction-wide markup/markdown)
    double global_trade_mod = 1.0;

    // Calculate price modifier for an item based on supply/demand
    double get_price_modifier( const item &it ) const;

    // Get price modifier for a specific category
    double get_category_modifier( econ_category cat ) const;

    // Record a purchase (player buys from faction → faction supply decreases)
    void record_purchase( const item &it, int amount = 1 );

    // Record a sale (player sells to faction → faction supply increases)
    void record_sale( const item &it, int amount = 1 );

    // Daily economy tick: drift supply/demand toward equilibrium
    void update( const time_point &now );

    // Initialize default category data
    void init_defaults();

    void serialize( JsonOut &json ) const;
    void deserialize( const JsonObject &jo );
};

// Global manager for all faction economies
class faction_economy_manager
{
    public:
        // Get economy for a faction (creates default if missing)
        faction_economy &get( const faction_id &fid );
        const faction_economy *get_if_exists( const faction_id &fid ) const;

        // Update all faction economies (call once per day)
        void update_all( const time_point &now );

        // Load faction economy profiles from JSON
        static void load( const JsonObject &jo );
        static void reset();

        void serialize( JsonOut &json ) const;
        void deserialize( const JsonValue &jv );

    private:
        std::map<faction_id, faction_economy> economies_;

        // Static templates loaded from JSON
        static std::map<faction_id, faction_economy> templates_;
};

// Global access
faction_economy_manager &get_faction_economy_manager();

#endif // CATA_SRC_FACTION_ECONOMY_H
