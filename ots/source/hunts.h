#ifndef YUROTS_HUNTS_H
#define YUROTS_HUNTS_H

#include "position.h"
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

class Game;
class Player;
class Creature;
class Container;
class Item;
class NetworkMessage;

// Private map copies use reserved, non-overlapping coordinate cells. All access,
// movement, damage, respawns, loot transfers and lifecycle stay on this server.
class HuntManager {
  public:
    HuntManager();
    void load(Game *game, const std::string &spawnFile);
    void tick();
    bool validateCatalog();
    void request(Player *player, NetworkMessage &msg);
    void manualInput(Player *player);
    void leave(Player *player, const std::string &reason = "Hunt finished");
    void onRemoved(Creature *creature);
    void onKilled(Creature *creature, Container *corpse);
    bool canTravel(const Creature *creature, const Position &from, const Position &to) const;
    bool isReserved(const Position &pos) const;
    bool isAuto(const Player *player) const;

  private:
    struct SpawnPoint {
        std::string name;
        Position pos;
        unsigned seconds;
        unsigned long creatureId;
        uint64_t readyAt;
    };
    struct Definition {
        unsigned id;
        std::string name, primary;
        Position center;
        int radius;
        std::vector<SpawnPoint> spawns;
    };
    struct Member {
        bool automatic, loot;
        uint64_t started, healAt;
        long long startExperience;
        unsigned kills, items;
        unsigned long target;
        char fight, follow;
        std::string activity;
    };
    struct Room {
        unsigned id, slot, huntId, limit;
        unsigned long party;
        int dx, dy, x0, y0, x1, y1;
        bool closing;
        uint64_t emptyAt, sendAt;
        std::set<unsigned long> allowed;
        std::map<unsigned long, Member> members;
        std::vector<Position> tiles;
        std::vector<SpawnPoint> spawns;
        Position entry;
    };
    Game *game;
    bool scheduled;
    unsigned nextRoom;
    std::vector<Definition> catalog;
    std::map<unsigned, Room> rooms;
    std::set<unsigned long> transitioning;
    std::map<unsigned long, uint64_t> lastRequest;

    Room *roomFor(const Player *player);
    const Room *roomAt(const Position &position) const;
    bool contains(const Room &room, const Position &position) const;
    bool walkable(const Room &room, const Position &position, bool ignoreCreatures = false) const;
    std::vector<Position> route(const Room &room, Player *player, const Position &target,
                                bool adjacent) const;
    std::string summary(const Definition &def, bool detailed) const;
    void send(Player *player, const std::string &json);
    void error(Player *player, const std::string &message);
    void state(Player *player, Room *room, const std::string &ended = "");
    void start(Player *player, unsigned id, unsigned mode, bool loot);
    bool create(Room &room, const Definition &def);
    void spawn(Room &room);
    void runPlayer(Room &room, Player *player, Member &member);
    void cleanup(Room &room);
};

extern HuntManager g_hunts;
#endif
