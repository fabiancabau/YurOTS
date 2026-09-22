#include "hunts.h"
#include "game.h"
#include "luascript.h"
#include "map.h"
#include "monster.h"
#include "monsters.h"
#include "networkmessage.h"
#include "player.h"
#include "protocol.h"
#include <algorithm>
#include <boost/bind.hpp>
#include <libxml/parser.h>
#include <limits>
#include <queue>
#include <sstream>

extern Monsters g_monsters;
extern LuaScript g_config;
HuntManager g_hunts;
namespace {
const unsigned MAX_ROOMS = 64;
const int ORIGIN = 4096, CELL = 512, WIDTH = 8;
std::string quote(const std::string &s) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c == '\n')
            out << "\\n";
        else if (c == '\r')
            out << "\\r";
        else if (c == '\t')
            out << "\\t";
        else if (c >= 32 && c < 128)
            out << c;
        else {
            const char *hex = "0123456789abcdef";
            out << "\\u00" << hex[c >> 4] << hex[c & 15];
        }
    }
    out << '"';
    return out.str();
}
std::string prop(xmlNodePtr n, const char *key, const std::string &fallback = "") {
    xmlChar *value = xmlGetProp(n, BAD_CAST key);
    if (!value)
        return fallback;
    std::string s(reinterpret_cast<char *>(value));
    xmlFree(value);
    return s;
}
std::string positionJSON(const Position &p) {
    std::ostringstream s;
    s << "{\"x\":" << p.x << ",\"y\":" << p.y << ",\"z\":" << p.z << "}";
    return s.str();
}
MonsterType *monsterType(const std::string &name) {
    return g_monsters.getMonsterType(g_monsters.getIdByName(name));
}
int distance(const Position &a, const Position &b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}
Item *scenery(Item *source, const Position &pos) {
    if (!source || source->isPickupable() || Item::items[source->getID()].isTeleport())
        return NULL;
    Item *item = Item::CreateItem(source->getID(), source->getItemCountOrSubtype());
    item->pos = pos;
#ifdef YUR_CLEAN_MAP
    item->decoration = true;
#endif
    return item; // no quest IDs, unique IDs, container contents or external portals
}
} // namespace

HuntManager::HuntManager() : game(NULL), scheduled(false), nextRoom(1) {}
void HuntManager::load(Game *value, const std::string &file) {
    if (scheduled)
        return;
    game = value;
    xmlDocPtr doc = xmlReadFile(file.c_str(), NULL, XML_PARSE_NONET);
    if (!doc)
        return;
    unsigned id = 0;
    for (xmlNodePtr node = xmlDocGetRootElement(doc)->children; node; node = node->next) {
        if (xmlStrcmp(node->name, BAD_CAST "spawn"))
            continue;
        Definition def;
        def.id = ++id;
        def.center =
            Position(atoi(prop(node, "centerx").c_str()), atoi(prop(node, "centery").c_str()),
                     atoi(prop(node, "centerz").c_str()));
        def.radius = std::max(1, std::min(64, atoi(prop(node, "radius", "5").c_str())));
        std::map<std::string, unsigned> counts;
        for (xmlNodePtr mon = node->children; mon; mon = mon->next) {
            if (xmlStrcmp(mon->name, BAD_CAST "monster"))
                continue;
            SpawnPoint point;
            point.name = prop(mon, "name");
            if (!monsterType(point.name))
                continue;
            point.pos = Position(def.center.x + atoi(prop(mon, "x").c_str()),
                                 def.center.y + atoi(prop(mon, "y").c_str()), def.center.z);
            point.seconds = std::max(5, std::min(3600, atoi(prop(mon, "spawntime", "60").c_str())));
            point.creatureId = 0;
            point.readyAt = 0;
            def.spawns.push_back(point);
            counts[point.name]++;
        }
        if (def.spawns.empty())
            continue;
        std::string primary = def.spawns[0].name;
        for (const auto &count : counts)
            if (count.second > counts[primary])
                primary = count.first;
        def.primary = primary;
        def.name = primary + (def.center.z > 7 ? " Caverns" : " Grounds");
        if (primary == "Rat" || primary == "Cave Rat")
            def.name = "Rat Cellars";
        if (primary == "Spider")
            def.name = "Spider Nest";
        catalog.push_back(def);
    }
    xmlFreeDoc(doc);
    scheduled = true;
    game->addEvent(makeTask(200, boost::bind(&HuntManager::tick, this)));
    std::cout << ":: Hunt catalog: " << catalog.size() << " spawn areas" << std::endl;
}

bool HuntManager::isReserved(const Position &p) const {
    return p.x >= ORIGIN && p.x < ORIGIN + WIDTH * CELL && p.y >= ORIGIN &&
           p.y < ORIGIN + WIDTH * CELL;
}
bool HuntManager::contains(const Room &r, const Position &p) const {
    return p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1 && p.z >= 0 && p.z < 16;
}
const HuntManager::Room *HuntManager::roomAt(const Position &p) const {
    if (!isReserved(p))
        return NULL;
    for (const auto &room : rooms)
        if (contains(room.second, p))
            return &room.second;
    return NULL;
}
HuntManager::Room *HuntManager::roomFor(const Player *p) {
    auto it = rooms.find(p->huntInstance);
    return it == rooms.end() ? NULL : &it->second;
}
bool HuntManager::canTravel(const Creature *creature, const Position &from,
                            const Position &to) const {
    if (!isReserved(from) && !isReserved(to))
        return true;
    if (creature && transitioning.count(creature->getID()))
        return true;
    const Room *a = roomAt(from);
    const Room *b = roomAt(to);
    if (!a || a != b)
        return false;
    const Player *p = dynamic_cast<const Player *>(creature);
    return !p || (p->huntInstance == a->id && a->members.count(p->getID()));
}
bool HuntManager::isAuto(const Player *player) const {
    auto r = rooms.find(player->huntInstance);
    if (r == rooms.end())
        return false;
    auto m = r->second.members.find(player->getID());
    return m != r->second.members.end() && m->second.automatic;
}
void HuntManager::send(Player *p, const std::string &json) {
    if (!p || !p->client || json.size() > 15000)
        return;
    NetworkMessage msg;
    msg.AddByte(0xf0);
    msg.AddByte(1);
    msg.AddString(json);
    p->client->sendNetworkMessage(&msg);
}
void HuntManager::error(Player *p, const std::string &message) {
    send(p, "{\"event\":\"error\",\"message\":" + quote(message) + "}");
}

std::string HuntManager::summary(const Definition &d, bool detail) const {
    std::map<std::string, unsigned> counts;
    for (const auto &s : d.spawns)
        counts[s.name]++;
    std::ostringstream out;
    out << "{\"id\":" << d.id << ",\"name\":" << quote(d.name)
        << ",\"center\":" << positionJSON(d.center) << ",\"spawns\":" << d.spawns.size()
        << ",\"radius\":" << d.radius << ",\"monsters\":[";
    bool first = true;
    std::map<unsigned, LootBlock> loot;
    std::vector<std::pair<std::string, unsigned>> ordered(counts.begin(), counts.end());
    std::stable_sort(
        ordered.begin(), ordered.end(),
        [&d](const std::pair<std::string, unsigned> &a, const std::pair<std::string, unsigned> &b) {
            if (a.first == d.primary)
                return b.first != d.primary;
            if (b.first == d.primary)
                return false;
            return a.second > b.second;
        });
    for (const auto &count : ordered) {
        MonsterType *type = monsterType(count.first);
        if (!type)
            continue;
        if (!first)
            out << ',';
        first = false;
        out << "{\"name\":" << quote(count.first) << ",\"count\":" << count.second
            << ",\"look\":" << type->looktype;
        if (detail)
            out << ",\"health\":" << type->health_max << ",\"experience\":" << type->experience;
        out << '}';
        for (const auto &item : type->lootItems) {
            auto it = loot.find(item.id);
            if (it == loot.end() || it->second.chance1 < item.chance1)
                loot[item.id] = item;
        }
    }
    out << ']';
    if (detail) {
        out << ",\"respawnSeconds\":" << d.spawns.front().seconds << ",\"loot\":[";
        first = true;
        unsigned count = 0;
        for (const auto &pair : loot) {
            if (++count > 80)
                break;
            const LootBlock &block = pair.second;
            const ItemType &item = Item::items[block.id];
            if (!first)
                out << ',';
            first = false;
            out << "{\"id\":" << item.clientId << ",\"name\":" << quote(item.name)
                << ",\"chance\":" << (double(block.chance1) * 100.0 / CHANCE_MAX)
                << ",\"count\":" << block.countmax << '}';
        }
        out << ']';
    }
    out << '}';
    return out.str();
}

void HuntManager::request(Player *p, NetworkMessage &msg) {
    if (!game) {
        error(p, "Hunts are not available on this map.");
        return;
    }
    OTSYS_THREAD_LOCK_CLASS lock(game->gameLock, "HuntManager::request");
    if (msg.getRemaining() < 1)
        return;
    const unsigned action = msg.GetByte();
    if (action == 0) {
        if (msg.getRemaining() != 2)
            return;
        const unsigned offset = msg.GetU16(), end = std::min<unsigned>(catalog.size(), offset + 10);
        std::ostringstream out;
        out << "{\"event\":\"catalog\",\"total\":" << catalog.size() << ",\"offset\":" << offset
            << ",\"next\":" << end << ",\"hunts\":[";
        for (unsigned i = offset; i < end; i++) {
            if (i > offset)
                out << ',';
            out << summary(catalog[i], false);
        }
        out << "]}";
        send(p, out.str());
        return;
    }
    if (action == 6) {
        if (msg.getRemaining() != 2)
            return;
        unsigned id = msg.GetU16();
        for (const auto &d : catalog)
            if (d.id == id) {
                send(p, "{\"event\":\"detail\",\"hunt\":" + summary(d, true) + "}");
                return;
            }
        error(p, "This hunt is not available.");
        return;
    }
    if (action == 5) {
        state(p, roomFor(p));
        return;
    }
    if (action == 7) {
        if (p->access < g_config.ACCESS_PROTECT) {
            error(p, "This command is restricted to game masters.");
            return;
        }
        size_t tiles = 0, members = 0, monsters = 0;
        for (const auto &r : rooms) {
            tiles += r.second.tiles.size();
            members += r.second.members.size();
            for (const auto &sp : r.second.spawns)
                if (sp.creatureId)
                    monsters++;
        }
        std::ostringstream out;
        out << "{\"event\":\"diagnostics\",\"instances\":" << rooms.size() << ",\"tiles\":" << tiles
            << ",\"members\":" << members << ",\"monsters\":" << monsters << '}';
        send(p, out.str());
        return;
    }
    const uint64_t now = OTSYS_TIME();
    if (lastRequest[p->getID()] + 250 > now)
        return;
    lastRequest[p->getID()] = now;
    if (action == 1) {
        if (msg.getRemaining() != 4)
            return;
        unsigned id = msg.GetU16(), mode = msg.GetByte();
        bool loot = msg.GetByte() != 0;
        start(p, id, mode, loot);
        return;
    }
    Room *room = roomFor(p);
    if (!room) {
        state(p, NULL);
        return;
    }
    auto member = room->members.find(p->getID());
    if (member == room->members.end())
        return;
    if (action == 2) {
        member->second.automatic = false;
        member->second.target = 0;
        member->second.activity = "Paused";
        game->playerSetAttackedCreature(p, 0);
        state(p, room);
    } else if (action == 3) {
        member->second.automatic = true;
        member->second.activity = "Finding a target";
        state(p, room);
    } else if (action == 4)
        leave(p);
}

void HuntManager::state(Player *p, Room *room, const std::string &ended) {
    std::ostringstream out;
    out << "{\"event\":\"state\",\"active\":" << (room ? "true" : "false");
    if (room && room->members.count(p->getID())) {
        const Member &m = room->members[p->getID()];
        const Definition *def = NULL;
        for (const auto &d : catalog)
            if (d.id == room->huntId)
                def = &d;
        unsigned alive = 0;
        uint64_t next = std::numeric_limits<uint64_t>::max();
        for (const auto &spawn : room->spawns) {
            if (spawn.creatureId)
                alive++;
            else
                next = std::min(next, spawn.readyAt);
        }
        out << ",\"instance\":" << room->id << ",\"huntId\":" << room->huntId
            << ",\"name\":" << quote(def ? def->name : "Hunt")
            << ",\"automatic\":" << (m.automatic ? "true" : "false")
            << ",\"activity\":" << quote(m.activity) << ",\"kills\":" << m.kills
            << ",\"items\":" << m.items
            << ",\"experience\":" << (p->getExperience() - m.startExperience)
            << ",\"seconds\":" << (OTSYS_TIME() - m.started) / 1000 << ",\"alive\":" << alive
            << ",\"spawnCount\":" << room->spawns.size() << ",\"members\":" << room->members.size()
            << ",\"target\":" << m.target << ",\"respawnIn\":"
            << (next == std::numeric_limits<uint64_t>::max()
                    ? 0
                    : (next > OTSYS_TIME() ? (next - OTSYS_TIME() + 999) / 1000 : 0));
    }
    if (!ended.empty())
        out << ",\"message\":" << quote(ended);
    out << '}';
    send(p, out.str());
}

bool HuntManager::walkable(const Room &room, const Position &p, bool ignore) const {
    if (!contains(room, p))
        return false;
    Tile *tile = game->getTile(p);
    return tile && tile->ground && !tile->floorChange() && !tile->getTeleportItem() &&
           tile->isBlocking(BLOCK_SOLID | BLOCK_PATHFIND, ignore) == RET_NOERROR;
}

bool HuntManager::create(Room &room, const Definition &d) {
    std::set<unsigned> used;
    for (const auto &r : rooms)
        used.insert(r.second.slot);
    room.slot = 0;
    while (used.count(room.slot) && room.slot < MAX_ROOMS)
        room.slot++;
    if (room.slot == MAX_ROOMS)
        return false;
    int minx = d.center.x - d.radius, maxx = d.center.x + d.radius, miny = d.center.y - d.radius,
        maxy = d.center.y + d.radius;
    for (const auto &s : d.spawns) {
        minx = std::min(minx, s.pos.x);
        maxx = std::max(maxx, s.pos.x);
        miny = std::min(miny, s.pos.y);
        maxy = std::max(maxy, s.pos.y);
    }
    minx = std::max(0, minx - 12);
    miny = std::max(0, miny - 12);
    maxx = std::min(4095, maxx + 12);
    maxy = std::min(4095, maxy + 12);
    if (maxx - minx > 200 || maxy - miny > 200)
        return false;
    room.dx = ORIGIN + (room.slot % WIDTH) * CELL + 64 - minx;
    room.dy = ORIGIN + (room.slot / WIDTH) * CELL + 64 - miny;
    room.x0 = minx + room.dx;
    room.x1 = maxx + room.dx;
    room.y0 = miny + room.dy;
    room.y1 = maxy + room.dy;
    for (int z = 0; z < 16; z++)
        for (int y = miny; y <= maxy; y++)
            for (int x = minx; x <= maxx; x++) {
                Tile *source = game->getTile(x, y, z);
                if (!source)
                    continue;
                Position pos(x + room.dx, y + room.dy, z);
                if (game->getTile(pos))
                    return false;
                Tile *tile = game->map->setTile(pos.x, pos.y, pos.z);
                room.tiles.push_back(pos);
                if (source->ground) {
                    Item *item = Item::CreateItem(source->ground->getID(),
                                                  source->ground->getItemCountOrSubtype());
                    item->pos = pos;
                    tile->addThing(item);
                }
                for (Item *sourceItem : source->topItems)
                    if (Item *item = scenery(sourceItem, pos))
                        tile->addThing(item);
                for (Item *sourceItem : source->downItems)
                    if (Item *item = scenery(sourceItem, pos))
                        tile->addThing(item);
            }
    // Choose a spawn-connected safe tile and normalize points into that component
    // so every selected spawn remains reachable even in split cave source areas.
    // Prefer the source floor. Some source spawns sit on isolated tree/lava
    // tiles, so use a connected floor in this same copied region if necessary.
    std::set<Position> component;
    const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    std::vector<int> floors;
    floors.push_back(d.center.z);
    for (int delta = 1; delta < 16; delta++) {
        if (d.center.z + delta < 16)
            floors.push_back(d.center.z + delta);
        if (d.center.z - delta >= 0)
            floors.push_back(d.center.z - delta);
    }
    const size_t required = d.spawns.size() + 5;
    for (int z : floors) {
        std::set<Position> visited, bestFloor;
        for (const auto &candidate : room.tiles) {
            if (candidate.z != z || visited.count(candidate) || !walkable(room, candidate, true))
                continue;
            std::set<Position> connected;
            std::queue<Position> queue;
            queue.push(candidate);
            visited.insert(candidate);
            while (!queue.empty()) {
                Position pos = queue.front();
                queue.pop();
                connected.insert(pos);
                for (const auto &dir : dirs) {
                    Position next(pos.x + dir[0], pos.y + dir[1], pos.z);
                    if (!visited.count(next) && walkable(room, next, true)) {
                        visited.insert(next);
                        queue.push(next);
                    }
                }
            }
            if (connected.size() > bestFloor.size())
                bestFloor.swap(connected);
        }
        if (bestFloor.size() >= required) {
            component.swap(bestFloor);
            break;
        }
        if (bestFloor.size() > component.size())
            component.swap(bestFloor);
    }
    if (component.size() < std::max<size_t>(3, d.spawns.size() + 1))
        return false;
    const Position seed = *component.begin();
    room.entry = seed;
    int best = std::numeric_limits<int>::max();
    Position center(d.center.x + room.dx, d.center.y + room.dy, d.center.z);
    for (const auto &p : component) {
        int score = distance(p, center);
        if (score < best) {
            best = score;
            room.entry = p;
        }
    }
    std::set<Position> taken;
    taken.insert(room.entry);
    for (SpawnPoint sp : d.spawns) {
        Position wanted(sp.pos.x + room.dx, sp.pos.y + room.dy, sp.pos.z), selected = seed;
        bool valid = false;
        best = std::numeric_limits<int>::max();
        for (const auto &p : component)
            if (!taken.count(p)) {
                int score = distance(p, wanted);
                if (score < best) {
                    best = score;
                    selected = p;
                    valid = true;
                }
            }
        if (!valid)
            break;
        taken.insert(selected);
        sp.pos = selected;
        sp.creatureId = 0;
        sp.readyAt = OTSYS_TIME() + 500;
        room.spawns.push_back(sp);
    }
    return room.spawns.size() == d.spawns.size();
}

void HuntManager::start(Player *p, unsigned id, unsigned mode, bool loot) {
    if (p->huntInstance) {
        error(p, "Leave your current hunt before starting another.");
        return;
    }
    if (mode > 2) {
        error(p, "Invalid pull size.");
        return;
    }
    if (p->inFightTicks >= 1000 || p->pzLocked) {
        error(p, "Finish your current fight before entering a hunt.");
        return;
    }
    const Definition *def = NULL;
    for (const auto &d : catalog)
        if (d.id == id)
            def = &d;
    if (!def) {
        error(p, "This hunt is not available.");
        return;
    }
    unsigned long group = p->party ? p->party : p->getID();
    Room *existing = NULL;
    for (auto &r : rooms)
        if (!r.second.closing && r.second.party == group) {
            existing = &r.second;
            break;
        }
    if (existing && (existing->huntId != id || !existing->allowed.count(p->getID()))) {
        error(p,
              "Your party already has a different hunt. Join that hunt or leave the party first.");
        return;
    }
    if (!existing) {
        Room r;
        r.id = nextRoom++;
        r.huntId = id;
        r.party = group;
        r.limit = mode == 0 ? 1 : mode == 1 ? 3 : 5;
        r.closing = false;
        r.emptyAt = 0;
        r.sendAt = 0;
        r.allowed.insert(p->getID());
        if (p->party)
            for (const auto &member : Player::listPlayer.list)
                if (member.second->party == p->party)
                    r.allowed.insert(member.first);
        if (!create(r, *def)) {
            cleanup(r);
            error(p, "This area has no free reachable instance, or all instances are busy.");
            return;
        }
        rooms[r.id] = r;
        existing = &rooms[r.id];
    }
    Room &room = *existing;
    Position entry = room.entry;
    if (!walkable(room, entry)) {
        bool found = false;
        for (int radius = 1; radius < 8 && !found; radius++)
            for (int y = -radius; y <= radius && !found; y++)
                for (int x = -radius; x <= radius && !found; x++) {
                    Position candidate(room.entry.x + x, room.entry.y + y, room.entry.z);
                    if (walkable(room, candidate)) {
                        entry = candidate;
                        found = true;
                    }
                }
        if (!found) {
            error(p, "There is no safe entry tile for you yet.");
            return;
        }
    }
    Member m;
    m.automatic = true;
    m.loot = loot;
    m.started = OTSYS_TIME();
    m.healAt = 0;
    m.startExperience = p->getExperience();
    m.kills = 0;
    m.items = 0;
    m.target = 0;
    m.fight = p->fightMode;
    m.follow = p->followMode;
    m.activity = "Finding a target";
    room.members[p->getID()] = m;
    p->huntReturn = p->pos;
    p->huntInstance = room.id;
    game->stopEvent(p->eventAutoWalk);
    p->pathlist.clear();
    p->eventAutoWalk = 0;
    game->playerSetAttackedCreature(p, 0);
    p->fightMode = 1;
    p->followMode = 0;
    transitioning.insert(p->getID());
    game->teleport(p, entry);
    transitioning.erase(p->getID());
    if (p->pos != entry) {
        room.members.erase(p->getID());
        p->huntInstance = 0;
        error(p, "Could not enter this hunt.");
        return;
    }
    state(p, &room);
    game->flushSendBuffers();
}

std::vector<Position> HuntManager::route(const Room &room, Player *player, const Position &target,
                                         bool adjacent) const {
    std::queue<Position> queue;
    std::map<Position, Position> prev;
    std::set<Position> visited;
    Position start = player->pos, end = start;
    bool found = false;
    queue.push(start);
    visited.insert(start);
    const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    while (!queue.empty() && visited.size() < 6000) {
        Position p = queue.front();
        queue.pop();
        if (p.z == target.z && (adjacent ? distance(p, target) <= 1 : p == target)) {
            end = p;
            found = true;
            break;
        }
        for (const auto &d : dirs) {
            Position q(p.x + d[0], p.y + d[1], p.z);
            if (!visited.count(q) && walkable(room, q)) {
                visited.insert(q);
                prev[q] = p;
                queue.push(q);
            }
        }
    }
    std::vector<Position> result;
    if (!found)
        return result;
    for (Position p = end; p != start; p = prev[p])
        result.push_back(p);
    std::reverse(result.begin(), result.end());
    return result;
}

void HuntManager::spawn(Room &room) {
    unsigned alive = 0;
    for (auto &sp : room.spawns) {
        if (sp.creatureId && !game->getCreatureByID(sp.creatureId)) {
            sp.creatureId = 0;
            sp.readyAt = OTSYS_TIME() + sp.seconds * 1000;
        }
        if (sp.creatureId)
            alive++;
    }
    for (auto &sp : room.spawns) {
        if (alive >= room.limit)
            break;
        if (sp.creatureId || sp.readyAt > OTSYS_TIME() || !walkable(room, sp.pos))
            continue;
        Monster *monster = Monster::createMonster(sp.name, game);
        if (!monster)
            continue;
        Position pos = sp.pos;
        monster->pos = pos;
        monster->masterPos = pos;
        if (game->placeCreature(pos, monster)) {
            sp.creatureId = monster->getID();
            alive++;
        } else {
            delete monster;
            sp.readyAt = OTSYS_TIME() + 1000;
        }
    }
}

void HuntManager::runPlayer(Room &room, Player *p, Member &m) {
    if (!m.automatic || p->isRemoved || p->health <= 0)
        return;
#ifdef TR_ANTI_AFK
    p->notAfk();
#endif
    if (p->getHealth() * 100 < p->healthmax * 65 && OTSYS_TIME() >= m.healAt) {
        m.healAt = OTSYS_TIME() + 2500;
        game->creatureSaySpell(p, "exura");
    }
    Creature *target = game->getCreatureByID(m.target);
    if (!target || target->health <= 0 || !contains(room, target->pos) ||
        target->pos.z != p->pos.z) {
        m.target = 0;
        target = NULL;
    }
    if (!target) {
        std::vector<std::pair<int, Creature *>> candidates;
        for (const auto &sp : room.spawns) {
            Creature *c = game->getCreatureByID(sp.creatureId);
            if (c && c->health > 0 && c->pos.z == p->pos.z)
                candidates.push_back(std::make_pair(distance(p->pos, c->pos), c));
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const std::pair<int, Creature *> &a, const std::pair<int, Creature *> &b) {
                      return a.first < b.first;
                  });
        for (const auto &candidate : candidates)
            if (candidate.first <= 1 || !route(room, p, candidate.second->pos, true).empty()) {
                target = candidate.second;
                break;
            }
        if (target)
            m.target = target->getID();
    }
    if (!target) {
        m.activity = "Waiting for respawns";
        return;
    }
    const bool visible =
        std::abs(p->pos.x - target->pos.x) <= 7 && std::abs(p->pos.y - target->pos.y) <= 5;
    if (visible && p->attackedCreature != target->getID())
        game->playerSetAttackedCreature(p, target->getID());
    else if (!visible && p->attackedCreature)
        game->playerSetAttackedCreature(p, 0);
    if (distance(p->pos, target->pos) <= 1) {
        m.activity = "Fighting " + target->getName();
        return;
    }
    m.activity = "Approaching " + target->getName();
    if (p->getSleepTicks() > 0)
        return;
    const std::vector<Position> path = route(room, p, target->pos, true);
    if (path.empty()) {
        m.target = 0;
        return;
    }
    const Position next = path.front();
    game->thingMove(p, p, next.x, next.y, next.z, 1);
}

void HuntManager::manualInput(Player *p) {
    if (!game)
        return;
    OTSYS_THREAD_LOCK_CLASS lock(game->gameLock, "HuntManager::manualInput");
    Room *room = roomFor(p);
    if (!room)
        return;
    auto m = room->members.find(p->getID());
    if (m == room->members.end() || !m->second.automatic)
        return;
    m->second.automatic = false;
    m->second.target = 0;
    m->second.activity = "Paused for manual control";
    game->playerSetAttackedCreature(p, 0);
    state(p, room);
}

void HuntManager::leave(Player *p, const std::string &reason) {
    if (!game)
        return;
    OTSYS_THREAD_LOCK_CLASS lock(game->gameLock, "HuntManager::leave");
    Room *room = roomFor(p);
    if (!room)
        return;
    auto it = room->members.find(p->getID());
    if (it == room->members.end())
        return;
    Member m = it->second;
    game->playerSetAttackedCreature(p, 0);
    game->stopEvent(p->eventAutoWalk);
    p->eventAutoWalk = 0;
    p->pathlist.clear();
    p->fightMode = m.fight;
    p->followMode = m.follow;
    p->inFightTicks = 0;
    p->pzLocked = false;
    p->sendIcons();
    Position dest = p->huntReturn;
    Tile *tile = game->getTile(dest);
    if (!tile || tile->isBlocking(BLOCK_SOLID, true) != RET_NOERROR)
        dest = p->masterPos;
    transitioning.insert(p->getID());
    game->teleport(p, dest);
    transitioning.erase(p->getID());
    if (isReserved(p->pos)) {
        error(p, "Unable to return safely. Try leaving again.");
        return;
    }
    std::ostringstream message;
    message << reason << ". " << m.kills << " kills, " << (p->getExperience() - m.startExperience)
            << " XP, " << m.items << " items collected.";
    room->members.erase(p->getID());
    p->huntInstance = 0;
    state(p, NULL, message.str());
    if (room->members.empty())
        room->emptyAt = OTSYS_TIME() + 1000;
}

void HuntManager::onRemoved(Creature *c) {
    Player *p = dynamic_cast<Player *>(c);
    if (!p)
        return;
    lastRequest.erase(p->getID());
    if (!p->huntInstance)
        return;
    Room *room = roomFor(p);
    if (!room)
        return;
    room->members.erase(p->getID());
    lastRequest.erase(p->getID());
    if (p->health <= 0)
        p->huntReturn = p->masterPos;
    state(p, NULL,
          p->health <= 0 ? "Hunt ended: defeated. Log in again at your temple." : "Hunt ended.");
    if (room->members.empty())
        room->emptyAt = OTSYS_TIME() + 1000;
    // Keep huntInstance on this removed Player object until its normal save has
    // stored huntReturn instead of the temporary private coordinate.
}

void HuntManager::onKilled(Creature *c, Container *corpse) {
    if (!dynamic_cast<Monster *>(c))
        return;
    for (auto &pair : rooms) {
        Room &room = pair.second;
        if (room.closing)
            continue;
        SpawnPoint *found = NULL;
        for (auto &sp : room.spawns)
            if (sp.creatureId == c->getID()) {
                found = &sp;
                break;
            }
        if (!found)
            continue;
        found->creatureId = 0;
        found->readyAt = OTSYS_TIME() + found->seconds * 1000;
        for (auto &member : room.members) {
            member.second.kills++;
            if (member.second.target == c->getID())
                member.second.target = 0;
        }
        if (corpse) {
            std::vector<Item *> loot(corpse->getItems(), corpse->getEnd());
            for (Item *item : loot)
                for (auto &member : room.members) {
                    Player *p = game->getPlayerByID(member.first);
                    if (!p || !member.second.loot || !p->addItem(item, true))
                        continue;
                    if (corpse->removeItem(item)) {
                        if (p->addItem(item)) {
                            member.second.items +=
                                item->isStackable() ? item->getItemCountOrSubtype() : 1;
                        } else
                            corpse->addItem(item);
                    }
                    break;
                }
        }
        return;
    }
}

void HuntManager::cleanup(Room &room) {
    room.closing = true;
    std::set<unsigned long> creatures;
    for (const auto &pos : room.tiles) {
        Tile *tile = game->getTile(pos);
        if (tile)
            for (Creature *c : tile->creatures)
                creatures.insert(c->getID());
    }
    for (auto id : creatures)
        if (Creature *c = game->getCreatureByID(id))
            if (!dynamic_cast<Player *>(c))
                game->removeCreature(c);
    for (const auto &pos : room.tiles) {
        Tile *tile = game->getTile(pos);
        if (!tile || !tile->creatures.empty())
            continue;
        std::vector<Item *> items;
        if (tile->ground)
            items.push_back(tile->ground);
        if (tile->splash)
            items.push_back(tile->splash);
        items.insert(items.end(), tile->topItems.begin(), tile->topItems.end());
        items.insert(items.end(), tile->downItems.begin(), tile->downItems.end());
        for (Item *item : items) {
            item->isRemoved = true;
            game->FreeThing(item);
        }
        game->map->tileMaps[pos.x & 0x7f][pos.y & 0x7f].erase((pos.x & 0xff80) << 16 |
                                                              (pos.y & 0xff80) << 1 | pos.z);
        delete tile;
    }
    room.tiles.clear();
}

void HuntManager::tick() {
    OTSYS_THREAD_LOCK_CLASS lock(game->gameLock, "HuntManager::tick");
    const uint64_t now = OTSYS_TIME();
    for (auto it = rooms.begin(); it != rooms.end();) {
        Room &room = it->second;
        if (room.members.empty()) {
            if (!room.emptyAt)
                room.emptyAt = now + 1000;
            if (now >= room.emptyAt) {
                cleanup(room);
                it = rooms.erase(it);
                continue;
            }
        } else {
            std::vector<unsigned long> ids;
            for (const auto &m : room.members)
                ids.push_back(m.first);
            for (auto id : ids) {
                Player *p = game->getPlayerByID(id);
                if (!p || p->isRemoved) {
                    room.members.erase(id);
                    continue;
                }
                if (room.party != p->getID() && p->party != room.party) {
                    leave(p, "Party membership changed");
                    continue;
                }
            }
            if (!room.members.empty()) {
                spawn(room);
                for (auto id : ids) {
                    auto member = room.members.find(id);
                    Player *p = game->getPlayerByID(id);
                    if (p && member != room.members.end())
                        runPlayer(room, p, member->second);
                }
            }
            if (now >= room.sendAt) {
                room.sendAt = now + 1000;
                for (const auto &member : room.members)
                    if (Player *p = game->getPlayerByID(member.first))
                        state(p, &room);
            }
        }
        ++it;
    }
    game->flushSendBuffers();
    game->addEvent(makeTask(200, boost::bind(&HuntManager::tick, this)));
}

bool HuntManager::validateCatalog() {
    if (!game || catalog.empty())
        return false;
    OTSYS_THREAD_LOCK_CLASS lock(game->gameLock, "HuntManager::validateCatalog");
    unsigned failed = 0;
    size_t tiles = 0;
    for (const auto &def : catalog) {
        Room room = {};
        bool ok = create(room, def);
        tiles += room.tiles.size();
        if (!ok) {
            failed++;
            std::cout << "HUNT INVALID " << def.id << " " << def.name << std::endl;
        }
        cleanup(room);
    }
    std::cout << "HUNT VALIDATION areas=" << catalog.size() << " failed=" << failed
              << " cloned_tiles=" << tiles << std::endl;
    return failed == 0;
}
