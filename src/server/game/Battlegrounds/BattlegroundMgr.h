/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __BATTLEGROUNDMGR_H
#define __BATTLEGROUNDMGR_H

#include "Common.h"
#include "DBCEnums.h"
#include "Battleground.h"
#include "BattlegroundQueue.h"
#include "UniqueTrackablePtr.h"
#include <unordered_map>

struct BattlemasterListEntry;

typedef std::map<uint32, Trinity::unique_trackable_ptr<Battleground>> BattlegroundContainer;
typedef std::set<uint32> BattlegroundClientIdsContainer;

typedef std::unordered_map<uint32, BattlegroundTypeId> BattleMastersMap;

enum BattlegroundMisc
{
    BATTLEGROUND_ARENA_POINT_DISTRIBUTION_DAY   = 86400,    // seconds in a day

    BATTLEGROUND_OBJECTIVE_UPDATE_INTERVAL      = 1000
};

struct BattlegroundData
{
    BattlegroundContainer m_Battlegrounds;
    BattlegroundClientIdsContainer m_ClientBattlegroundIds[MAX_BATTLEGROUND_BRACKETS];
    BGFreeSlotQueueContainer BGFreeSlotQueue;
};

struct BattlegroundTemplate
{
    BattlegroundTypeId Id;
    uint16 MinPlayersPerTeam;
    uint16 MaxPlayersPerTeam;
    uint8 MinLevel;
    uint8 MaxLevel;
    Position StartLocation[PVP_TEAMS_COUNT];
    float MaxStartDistSq;
    uint8 Weight;
    uint32 ScriptId;
    BattlemasterListEntry const* BattlemasterEntry;
    std::vector<int32> MapIDs;

    bool IsArena() const;
};

namespace WorldPackets
{
    namespace Battleground
    {
        struct BattlefieldStatusHeader;
        class BattlefieldStatusNone;
        class BattlefieldStatusNeedConfirmation;
        class BattlefieldStatusActive;
        class BattlefieldStatusQueued;
        class GroupJoinedBattleground;
        using BattlefieldStatusFailed = GroupJoinedBattleground;
    }
}

class TC_GAME_API BattlegroundMgr
{
    private:
        BattlegroundMgr();
        ~BattlegroundMgr();

    public:
        BattlegroundMgr(BattlegroundMgr const& right) = delete;
        BattlegroundMgr(BattlegroundMgr&& right) = delete;
        BattlegroundMgr& operator=(BattlegroundMgr const& right) = delete;
        BattlegroundMgr& operator=(BattlegroundMgr&& right) = delete;

        static BattlegroundMgr* instance();

        void Update(uint32 diff);

        /* Packet Building */
        static void BuildBattlegroundStatusHeader(WorldPackets::Battleground::BattlefieldStatusHeader* header, Battleground const* bg, uint32 queueSlot, BattlegroundQueueTypeId queueId);
        static void BuildBattlegroundStatusNone(WorldPackets::Battleground::BattlefieldStatusNone* battlefieldStatus, uint32 queueSlot);
        static void BuildBattlegroundStatusNeedConfirmation(WorldPackets::Battleground::BattlefieldStatusNeedConfirmation* battlefieldStatus, Battleground const* bg, uint32 queueSlot, uint32 timeout, BattlegroundQueueTypeId queueId);
        static void BuildBattlegroundStatusActive(WorldPackets::Battleground::BattlefieldStatusActive* battlefieldStatus, Battleground const* bg, Player const* player, uint32 queueSlot, BattlegroundQueueTypeId queueId);
        static void BuildBattlegroundStatusQueued(WorldPackets::Battleground::BattlefieldStatusQueued* battlefieldStatus, Battleground const* bg, uint32 queueSlot, uint32 joinTime, BattlegroundQueueTypeId queueId, uint32 avgWaitTime);
        static void BuildBattlegroundStatusFailed(WorldPackets::Battleground::BattlefieldStatusFailed* battlefieldStatus, GroupJoinBattlegroundResult result, ObjectGuid const* errorGuid = nullptr);
        static void BuildGroupJoinedBattlegroundPacket(WorldPackets::Battleground::GroupJoinedBattleground* groupJoinedBattleground, BattlegroundTypeId bgTypeId);

        void SendBattlegroundList(Player* player, ObjectGuid const& guid, BattlegroundTypeId bgTypeId);
        void SendAreaSpiritHealerQueryOpcode(Player* player, Battleground* bg, ObjectGuid guid);

        /* Battlegrounds */
        Battleground* GetBattlegroundThroughClientInstance(uint32 instanceId, BattlegroundTypeId bgTypeId);
        Battleground* GetBattleground(uint32 InstanceID, BattlegroundTypeId bgTypeId);
        Battleground* GetBattlegroundTemplate(BattlegroundTypeId bgTypeId);
        Battleground* CreateNewBattleground(BattlegroundTypeId bgTypeId, PvPDifficultyEntry const* bracketEntry, uint8 arenaType, bool isRated);

        void AddBattleground(Battleground* bg);
        void AddToBGFreeSlotQueue(BattlegroundTypeId bgTypeId, Battleground* bg);
        void RemoveFromBGFreeSlotQueue(BattlegroundTypeId bgTypeId, uint32 instanceId);
        BGFreeSlotQueueContainer& GetBGFreeSlotQueueStore(BattlegroundTypeId bgTypeId);

        void LoadBattlegroundTemplates();
        void DeleteAllBattlegrounds();

        void SendToBattleground(Player* player, uint32 InstanceID, BattlegroundTypeId bgTypeId);

        /* Battleground queues */
        bool IsValidQueueId(BattlegroundQueueTypeId bgQueueTypeId);
        BattlegroundQueue& GetBattlegroundQueue(BattlegroundQueueTypeId bgQueueTypeId) { return m_BattlegroundQueues.emplace(bgQueueTypeId, bgQueueTypeId).first->second; }
        void ScheduleQueueUpdate(uint32 arenaMatchmakerRating, BattlegroundQueueTypeId bgQueueTypeId);
        uint32 GetPrematureFinishTime() const;

        void ToggleArenaTesting();
        void ToggleTesting();

        void ResetHolidays();
        void SetHolidayActive(uint32 battlegroundId);

        bool isArenaTesting() const { return m_ArenaTesting; }
        bool isTesting() const { return m_Testing; }

        static bool IsRandomBattleground(uint32 battlemasterListId);
        static BattlegroundQueueTypeId BGQueueTypeId(BattlegroundTypeId bgTypeId, uint8 bracketId, uint8 arenaType);

        static HolidayIds BGTypeToWeekendHolidayId(BattlegroundTypeId bgTypeId);
        static BattlegroundTypeId WeekendHolidayIdToBGType(HolidayIds holiday);
        static bool IsBGWeekend(BattlegroundTypeId bgTypeId);

        uint32 GetMaxRatingDifference() const;
        uint32 GetRatingDiscardTimer()  const;
        void InitAutomaticArenaPointDistribution();
        void LoadBattleMastersEntry();
        void CheckBattleMasters();
        BattlegroundTypeId GetBattleMasterBG(uint32 entry) const
        {
            BattleMastersMap::const_iterator itr = mBattleMastersMap.find(entry);
            if (itr != mBattleMastersMap.end())
                return itr->second;
            return BATTLEGROUND_TYPE_NONE;
        }

    private:
        bool CreateBattleground(BattlegroundTemplate const* bgTemplate);
        uint32 CreateClientVisibleInstanceId(BattlegroundTypeId bgTypeId, BattlegroundBracketId bracket_id);
        static bool IsArenaType(BattlegroundTypeId bgTypeId);
        BattlegroundTypeId GetRandomBG(BattlegroundTypeId id);

        typedef std::map<BattlegroundTypeId, BattlegroundData> BattlegroundDataContainer;
        BattlegroundDataContainer bgDataStore;

        std::map<BattlegroundQueueTypeId, BattlegroundQueue> m_BattlegroundQueues;

        struct ScheduledQueueUpdate
        {
            uint32 ArenaMatchmakerRating;
            BattlegroundQueueTypeId QueueId;

            bool operator==(ScheduledQueueUpdate const& right) const = default;
        };

        std::vector<ScheduledQueueUpdate> m_QueueUpdateScheduler;
        uint32 m_NextRatedArenaUpdate;
        time_t m_NextAutoDistributionTime;
        uint32 m_AutoDistributionTimeChecker;
        uint32 m_UpdateTimer;
        bool   m_ArenaTesting;
        bool   m_Testing;
        BattleMastersMap mBattleMastersMap;

        BattlegroundTemplate const* GetBattlegroundTemplateByTypeId(BattlegroundTypeId id)
        {
            BattlegroundTemplateMap::const_iterator itr = _battlegroundTemplates.find(id);
            if (itr != _battlegroundTemplates.end())
                return &itr->second;
            return nullptr;
        }

        BattlegroundTemplate const* GetBattlegroundTemplateByMapId(uint32 mapId)
        {
            BattlegroundMapTemplateContainer::const_iterator itr = _battlegroundMapTemplates.find(mapId);
            if (itr != _battlegroundMapTemplates.end())
                return itr->second;
            return nullptr;
        }

        typedef std::map<BattlegroundTypeId, uint8 /*weight*/> BattlegroundSelectionWeightMap;

        typedef std::map<BattlegroundTypeId, BattlegroundTemplate> BattlegroundTemplateMap;
        typedef std::map<uint32 /*mapId*/, BattlegroundTemplate*> BattlegroundMapTemplateContainer;
        BattlegroundTemplateMap _battlegroundTemplates;
        BattlegroundMapTemplateContainer _battlegroundMapTemplates;

    //npcbot
public:
    BattlegroundDataContainer const& GetBgDataStore() const { return bgDataStore; }
    //end npcbot
};

#define sBattlegroundMgr BattlegroundMgr::instance()

#endif // __BATTLEGROUNDMGR_H
