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

#ifndef __BATTLEGROUNDEY_H
#define __BATTLEGROUNDEY_H

#include "Battleground.h"
#include "BattlegroundScore.h"
#include "Object.h"

enum BG_EY_Misc
{
    BG_EY_EVENT_START_BATTLE        = 13180, // Achievement: Flurry
    BG_EY_FLAG_RESPAWN_TIME         = (8*IN_MILLISECONDS),
    BG_EY_FPOINTS_TICK_TIME         = (2*IN_MILLISECONDS)
};

enum BG_EY_WorldStates
{
    EY_ALLIANCE_RESOURCES           = 2749,
    EY_HORDE_RESOURCES              = 2750,
    EY_ALLIANCE_BASE                = 2752,
    EY_HORDE_BASE                   = 2753,
    DRAENEI_RUINS_HORDE_CONTROL     = 2733,
    DRAENEI_RUINS_ALLIANCE_CONTROL  = 2732,
    DRAENEI_RUINS_UNCONTROL         = 2731,
    MAGE_TOWER_ALLIANCE_CONTROL     = 2730,
    MAGE_TOWER_HORDE_CONTROL        = 2729,
    MAGE_TOWER_UNCONTROL            = 2728,
    FEL_REAVER_HORDE_CONTROL        = 2727,
    FEL_REAVER_ALLIANCE_CONTROL     = 2726,
    FEL_REAVER_UNCONTROL            = 2725,
    BLOOD_ELF_HORDE_CONTROL         = 2724,
    BLOOD_ELF_ALLIANCE_CONTROL      = 2723,
    BLOOD_ELF_UNCONTROL             = 2722,
    PROGRESS_BAR_PERCENT_GREY       = 2720,                 //100 = empty (only grey), 0 = blue|red (no grey)
    PROGRESS_BAR_STATUS             = 2719,                 //50 init!, 48 ... hordak bere .. 33 .. 0 = full 100% hordacky, 100 = full alliance
    PROGRESS_BAR_SHOW               = 2718,                 //1 init, 0 druhy send - bez messagu, 1 = controlled aliance
    NETHERSTORM_FLAG                = 2757,
    //set to 2 when flag is picked up, and to 1 if it is dropped
    NETHERSTORM_FLAG_STATE_ALLIANCE = 2769,
    NETHERSTORM_FLAG_STATE_HORDE    = 2770
};

enum BG_EY_ProgressBarConsts
{
    BG_EY_POINT_MAX_CAPTURERS_COUNT     = 5,
    BG_EY_POINT_RADIUS                  = 70,
    BG_EY_PROGRESS_BAR_DONT_SHOW        = 0,
    BG_EY_PROGRESS_BAR_SHOW             = 1,
    BG_EY_PROGRESS_BAR_PERCENT_GREY     = 40,
    BG_EY_PROGRESS_BAR_STATE_MIDDLE     = 50,
    BG_EY_PROGRESS_BAR_HORDE_CONTROLLED = 0,
    BG_EY_PROGRESS_BAR_NEUTRAL_LOW      = 30,
    BG_EY_PROGRESS_BAR_NEUTRAL_HIGH     = 70,
    BG_EY_PROGRESS_BAR_ALI_CONTROLLED   = 100
};

enum BG_EY_Sounds
{
    //strange ids, but sure about them
    BG_EY_SOUND_FLAG_PICKED_UP_ALLIANCE = 8212,
    BG_EY_SOUND_FLAG_CAPTURED_HORDE     = 8213,
    BG_EY_SOUND_FLAG_PICKED_UP_HORDE    = 8174,
    BG_EY_SOUND_FLAG_CAPTURED_ALLIANCE  = 8173,
    BG_EY_SOUND_FLAG_RESET              = 8192
};

enum BG_EY_Spells
{
    BG_EY_NETHERSTORM_FLAG_SPELL        = 34976,
    BG_EY_PLAYER_DROPPED_FLAG_SPELL     = 34991
};

enum EYBattlegroundObjectEntry
{
    BG_OBJECT_A_DOOR_EY_ENTRY           = 184719,           //Alliance door
    BG_OBJECT_H_DOOR_EY_ENTRY           = 184720,           //Horde door
    BG_OBJECT_FLAG1_EY_ENTRY            = 184493,           //Netherstorm flag (generic)
    BG_OBJECT_FLAG2_EY_ENTRY            = 184141,           //Netherstorm flag (flagstand)
    BG_OBJECT_FLAG3_EY_ENTRY            = 184142,           //Netherstorm flag (flagdrop)
    BG_OBJECT_A_BANNER_EY_ENTRY         = 184381,           //Visual Banner (Alliance)
    BG_OBJECT_H_BANNER_EY_ENTRY         = 184380,           //Visual Banner (Horde)
    BG_OBJECT_N_BANNER_EY_ENTRY         = 184382,           //Visual Banner (Neutral)
    BG_OBJECT_BE_TOWER_CAP_EY_ENTRY     = 184080,           //BE Tower Cap Pt
    BG_OBJECT_FR_TOWER_CAP_EY_ENTRY     = 184081,           //Fel Reaver Cap Pt
    BG_OBJECT_HU_TOWER_CAP_EY_ENTRY     = 184082,           //Human Tower Cap Pt
    BG_OBJECT_DR_TOWER_CAP_EY_ENTRY     = 184083            //Draenei Tower Cap Pt
};

enum EYBattlegroundPointsTrigger
{
    TR_BLOOD_ELF_POINT        = 4476,
    TR_FEL_REAVER_POINT       = 4514,
    TR_MAGE_TOWER_POINT       = 4516,
    TR_DRAENEI_RUINS_POINT    = 4518,
    TR_BLOOD_ELF_BUFF         = 4568,
    TR_FEL_REAVER_BUFF        = 4569,
    TR_MAGE_TOWER_BUFF        = 4570,
    TR_DRAENEI_RUINS_BUFF     = 4571
};

enum EYBattlegroundGaveyards
{
    EY_GRAVEYARD_MAIN_ALLIANCE     = 1103,
    EY_GRAVEYARD_MAIN_HORDE        = 1104,
    EY_GRAVEYARD_FEL_REAVER       = 1105,
    EY_GRAVEYARD_BLOOD_ELF         = 1106,
    EY_GRAVEYARD_DRAENEI_RUINS     = 1107,
    EY_GRAVEYARD_MAGE_TOWER        = 1108
};

enum EYBattlegroundPoints
{
    FEL_REAVER     = 0,
    BLOOD_ELF       = 1,
    DRAENEI_RUINS   = 2,
    MAGE_TOWER      = 3,

    EY_PLAYERS_OUT_OF_POINTS  = 4,
    EY_POINTS_MAX             = 4
};

enum EYBattlegroundCreaturesTypes
{
    EY_SPIRIT_FEL_REAVER      = 0,
    EY_SPIRIT_BLOOD_ELF        = 1,
    EY_SPIRIT_DRAENEI_RUINS    = 2,
    EY_SPIRIT_MAGE_TOWER       = 3,
    EY_SPIRIT_MAIN_ALLIANCE    = 4,
    EY_SPIRIT_MAIN_HORDE       = 5,

    EY_TRIGGER_FEL_REAVER      = 6,
    EY_TRIGGER_BLOOD_ELF        = 7,
    EY_TRIGGER_DRAENEI_RUINS    = 8,
    EY_TRIGGER_MAGE_TOWER       = 9,

    BG_EY_CREATURES_MAX        = 10
};

enum EYBattlegroundObjectTypes
{
    BG_EY_OBJECT_DOOR_A                         = 0,
    BG_EY_OBJECT_DOOR_H                         = 1,
    BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER    = 2,
    BG_EY_OBJECT_A_BANNER_FEL_REAVER_LEFT      = 3,
    BG_EY_OBJECT_A_BANNER_FEL_REAVER_RIGHT     = 4,
    BG_EY_OBJECT_A_BANNER_BLOOD_ELF_CENTER      = 5,
    BG_EY_OBJECT_A_BANNER_BLOOD_ELF_LEFT        = 6,
    BG_EY_OBJECT_A_BANNER_BLOOD_ELF_RIGHT       = 7,
    BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_CENTER  = 8,
    BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_LEFT    = 9,
    BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_RIGHT   = 10,
    BG_EY_OBJECT_A_BANNER_MAGE_TOWER_CENTER     = 11,
    BG_EY_OBJECT_A_BANNER_MAGE_TOWER_LEFT       = 12,
    BG_EY_OBJECT_A_BANNER_MAGE_TOWER_RIGHT      = 13,
    BG_EY_OBJECT_H_BANNER_FEL_REAVER_CENTER    = 14,
    BG_EY_OBJECT_H_BANNER_FEL_REAVER_LEFT      = 15,
    BG_EY_OBJECT_H_BANNER_FEL_REAVER_RIGHT     = 16,
    BG_EY_OBJECT_H_BANNER_BLOOD_ELF_CENTER      = 17,
    BG_EY_OBJECT_H_BANNER_BLOOD_ELF_LEFT        = 18,
    BG_EY_OBJECT_H_BANNER_BLOOD_ELF_RIGHT       = 19,
    BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_CENTER  = 20,
    BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_LEFT    = 21,
    BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_RIGHT   = 22,
    BG_EY_OBJECT_H_BANNER_MAGE_TOWER_CENTER     = 23,
    BG_EY_OBJECT_H_BANNER_MAGE_TOWER_LEFT       = 24,
    BG_EY_OBJECT_H_BANNER_MAGE_TOWER_RIGHT      = 25,
    BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER    = 26,
    BG_EY_OBJECT_N_BANNER_FEL_REAVER_LEFT      = 27,
    BG_EY_OBJECT_N_BANNER_FEL_REAVER_RIGHT     = 28,
    BG_EY_OBJECT_N_BANNER_BLOOD_ELF_CENTER      = 29,
    BG_EY_OBJECT_N_BANNER_BLOOD_ELF_LEFT        = 30,
    BG_EY_OBJECT_N_BANNER_BLOOD_ELF_RIGHT       = 31,
    BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_CENTER  = 32,
    BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_LEFT    = 33,
    BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_RIGHT   = 34,
    BG_EY_OBJECT_N_BANNER_MAGE_TOWER_CENTER     = 35,
    BG_EY_OBJECT_N_BANNER_MAGE_TOWER_LEFT       = 36,
    BG_EY_OBJECT_N_BANNER_MAGE_TOWER_RIGHT      = 37,
    BG_EY_OBJECT_TOWER_CAP_FEL_REAVER          = 38,
    BG_EY_OBJECT_TOWER_CAP_BLOOD_ELF            = 39,
    BG_EY_OBJECT_TOWER_CAP_DRAENEI_RUINS        = 40,
    BG_EY_OBJECT_TOWER_CAP_MAGE_TOWER           = 41,
    BG_EY_OBJECT_FLAG_NETHERSTORM               = 42,
    BG_EY_OBJECT_FLAG_FEL_REAVER               = 43,
    BG_EY_OBJECT_FLAG_BLOOD_ELF                 = 44,
    BG_EY_OBJECT_FLAG_DRAENEI_RUINS             = 45,
    BG_EY_OBJECT_FLAG_MAGE_TOWER                = 46,
    //buffs
    BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER          = 47,
    BG_EY_OBJECT_REGENBUFF_FEL_REAVER          = 48,
    BG_EY_OBJECT_BERSERKBUFF_FEL_REAVER        = 49,
    BG_EY_OBJECT_SPEEDBUFF_BLOOD_ELF            = 50,
    BG_EY_OBJECT_REGENBUFF_BLOOD_ELF            = 51,
    BG_EY_OBJECT_BERSERKBUFF_BLOOD_ELF          = 52,
    BG_EY_OBJECT_SPEEDBUFF_DRAENEI_RUINS        = 53,
    BG_EY_OBJECT_REGENBUFF_DRAENEI_RUINS        = 54,
    BG_EY_OBJECT_BERSERKBUFF_DRAENEI_RUINS      = 55,
    BG_EY_OBJECT_SPEEDBUFF_MAGE_TOWER           = 56,
    BG_EY_OBJECT_REGENBUFF_MAGE_TOWER           = 57,
    BG_EY_OBJECT_BERSERKBUFF_MAGE_TOWER         = 58,
    BG_EY_OBJECT_MAX                            = 59
};

#define BG_EY_NotEYWeekendHonorTicks    260
#define BG_EY_EYWeekendHonorTicks       160

enum BG_EY_Score
{
    BG_EY_WARNING_NEAR_VICTORY_SCORE    = 1400,
    BG_EY_MAX_TEAM_SCORE                = 1600
};

enum BG_EY_FlagState
{
    BG_EY_FLAG_STATE_ON_BASE      = 0,
    BG_EY_FLAG_STATE_WAIT_RESPAWN = 1,
    BG_EY_FLAG_STATE_ON_PLAYER    = 2,
    BG_EY_FLAG_STATE_ON_GROUND    = 3
};

enum EYBattlegroundPointState
{
    EY_POINT_NO_OWNER           = 0,
    EY_POINT_STATE_UNCONTROLLED = 0,
    EY_POINT_UNDER_CONTROL      = 3
};

enum BG_EY_Objectives
{
    EY_OBJECTIVE_CAPTURE_FLAG   = 183
};

enum BG_EY_BroadcastTexts
{
    BG_EY_TEXT_ALLIANCE_TAKEN_FEL_REAVER_RUINS  = 17828,
    BG_EY_TEXT_HORDE_TAKEN_FEL_REAVER_RUINS     = 17829,
    BG_EY_TEXT_ALLIANCE_LOST_FEL_REAVER_RUINS   = 17835,
    BG_EY_TEXT_HORDE_LOST_FEL_REAVER_RUINS      = 17836,

    BG_EY_TEXT_ALLIANCE_TAKEN_BLOOD_ELF_TOWER   = 17819,
    BG_EY_TEXT_HORDE_TAKEN_BLOOD_ELF_TOWER      = 17823,
    BG_EY_TEXT_ALLIANCE_LOST_BLOOD_ELF_TOWER    = 17831,
    BG_EY_TEXT_HORDE_LOST_BLOOD_ELF_TOWER       = 17832,

    BG_EY_TEXT_ALLIANCE_TAKEN_DRAENEI_RUINS     = 17827,
    BG_EY_TEXT_HORDE_TAKEN_DRAENEI_RUINS        = 17826,
    BG_EY_TEXT_ALLIANCE_LOST_DRAENEI_RUINS      = 17833,
    BG_EY_TEXT_HORDE_LOST_DRAENEI_RUINS         = 17834,

    BG_EY_TEXT_ALLIANCE_TAKEN_MAGE_TOWER        = 17824,
    BG_EY_TEXT_HORDE_TAKEN_MAGE_TOWER           = 17825,
    BG_EY_TEXT_ALLIANCE_LOST_MAGE_TOWER         = 17837,
    BG_EY_TEXT_HORDE_LOST_MAGE_TOWER            = 17838,

    BG_EY_TEXT_TAKEN_FLAG                       = 18359,
    BG_EY_TEXT_FLAG_DROPPED                     = 18361,
    BG_EY_TEXT_FLAG_RESET                       = 18364,
    BG_EY_TEXT_ALLIANCE_CAPTURED_FLAG           = 18375,
    BG_EY_TEXT_HORDE_CAPTURED_FLAG              = 18384,
};

struct BattlegroundEYPointIconsStruct
{
    BattlegroundEYPointIconsStruct(uint32 _WorldStateControlIndex, uint32 _WorldStateAllianceControlledIndex, uint32 _WorldStateHordeControlledIndex)
        : WorldStateControlIndex(_WorldStateControlIndex), WorldStateAllianceControlledIndex(_WorldStateAllianceControlledIndex), WorldStateHordeControlledIndex(_WorldStateHordeControlledIndex) { }
    uint32 WorldStateControlIndex;
    uint32 WorldStateAllianceControlledIndex;
    uint32 WorldStateHordeControlledIndex;
};

Position const BG_EY_TriggerPositions[EY_POINTS_MAX] =
{
    {2044.28f, 1729.68f, 1189.96f, 0.017453f},  // FEL_REAVER center
    {2048.83f, 1393.65f, 1194.49f, 0.20944f},   // BLOOD_ELF center
    {2286.56f, 1402.36f, 1197.11f, 3.72381f},   // DRAENEI_RUINS center
    {2284.48f, 1731.23f, 1189.99f, 2.89725f}    // MAGE_TOWER center
};

struct BattlegroundEYLosingPointStruct
{
    BattlegroundEYLosingPointStruct(uint32 _SpawnNeutralObjectType, uint32 _DespawnObjectTypeAlliance, uint32 _MessageIdAlliance, uint32 _DespawnObjectTypeHorde, uint32 _MessageIdHorde)
        : SpawnNeutralObjectType(_SpawnNeutralObjectType),
        DespawnObjectTypeAlliance(_DespawnObjectTypeAlliance), MessageIdAlliance(_MessageIdAlliance),
        DespawnObjectTypeHorde(_DespawnObjectTypeHorde), MessageIdHorde(_MessageIdHorde)
    { }

    uint32 SpawnNeutralObjectType;
    uint32 DespawnObjectTypeAlliance;
    uint32 MessageIdAlliance;
    uint32 DespawnObjectTypeHorde;
    uint32 MessageIdHorde;
};

struct BattlegroundEYCapturingPointStruct
{
    BattlegroundEYCapturingPointStruct(uint32 _DespawnNeutralObjectType, uint32 _SpawnObjectTypeAlliance, uint32 _MessageIdAlliance, uint32 _SpawnObjectTypeHorde, uint32 _MessageIdHorde, uint32 _GraveyardId)
        : DespawnNeutralObjectType(_DespawnNeutralObjectType),
        SpawnObjectTypeAlliance(_SpawnObjectTypeAlliance), MessageIdAlliance(_MessageIdAlliance),
        SpawnObjectTypeHorde(_SpawnObjectTypeHorde), MessageIdHorde(_MessageIdHorde),
        GraveyardId(_GraveyardId)
    { }

    uint32 DespawnNeutralObjectType;
    uint32 SpawnObjectTypeAlliance;
    uint32 MessageIdAlliance;
    uint32 SpawnObjectTypeHorde;
    uint32 MessageIdHorde;
    uint32 GraveyardId;
};

const uint8  BG_EY_TickPoints[EY_POINTS_MAX] = {1, 2, 5, 10};
const uint32 BG_EY_FlagPoints[EY_POINTS_MAX] = {75, 85, 100, 500};

//constant arrays:
const BattlegroundEYPointIconsStruct m_PointsIconStruct[EY_POINTS_MAX] =
{
    BattlegroundEYPointIconsStruct(FEL_REAVER_UNCONTROL, FEL_REAVER_ALLIANCE_CONTROL, FEL_REAVER_HORDE_CONTROL),
    BattlegroundEYPointIconsStruct(BLOOD_ELF_UNCONTROL, BLOOD_ELF_ALLIANCE_CONTROL, BLOOD_ELF_HORDE_CONTROL),
    BattlegroundEYPointIconsStruct(DRAENEI_RUINS_UNCONTROL, DRAENEI_RUINS_ALLIANCE_CONTROL, DRAENEI_RUINS_HORDE_CONTROL),
    BattlegroundEYPointIconsStruct(MAGE_TOWER_UNCONTROL, MAGE_TOWER_ALLIANCE_CONTROL, MAGE_TOWER_HORDE_CONTROL)
};
const BattlegroundEYLosingPointStruct m_LosingPointTypes[EY_POINTS_MAX] =
{
    BattlegroundEYLosingPointStruct(BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER, BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER, BG_EY_TEXT_ALLIANCE_LOST_FEL_REAVER_RUINS, BG_EY_OBJECT_H_BANNER_FEL_REAVER_CENTER, BG_EY_TEXT_HORDE_LOST_FEL_REAVER_RUINS),
    BattlegroundEYLosingPointStruct(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_CENTER, BG_EY_OBJECT_A_BANNER_BLOOD_ELF_CENTER, BG_EY_TEXT_ALLIANCE_LOST_BLOOD_ELF_TOWER, BG_EY_OBJECT_H_BANNER_BLOOD_ELF_CENTER, BG_EY_TEXT_HORDE_LOST_BLOOD_ELF_TOWER),
    BattlegroundEYLosingPointStruct(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_CENTER, BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_CENTER, BG_EY_TEXT_ALLIANCE_LOST_DRAENEI_RUINS, BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_CENTER, BG_EY_TEXT_HORDE_LOST_DRAENEI_RUINS),
    BattlegroundEYLosingPointStruct(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_CENTER, BG_EY_OBJECT_A_BANNER_MAGE_TOWER_CENTER, BG_EY_TEXT_ALLIANCE_LOST_MAGE_TOWER, BG_EY_OBJECT_H_BANNER_MAGE_TOWER_CENTER, BG_EY_TEXT_HORDE_LOST_MAGE_TOWER)
};
const BattlegroundEYCapturingPointStruct m_CapturingPointTypes[EY_POINTS_MAX] =
{
    BattlegroundEYCapturingPointStruct(BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER, BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER, BG_EY_TEXT_ALLIANCE_TAKEN_FEL_REAVER_RUINS, BG_EY_OBJECT_H_BANNER_FEL_REAVER_CENTER, BG_EY_TEXT_HORDE_TAKEN_FEL_REAVER_RUINS, EY_GRAVEYARD_FEL_REAVER),
    BattlegroundEYCapturingPointStruct(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_CENTER, BG_EY_OBJECT_A_BANNER_BLOOD_ELF_CENTER, BG_EY_TEXT_ALLIANCE_TAKEN_BLOOD_ELF_TOWER, BG_EY_OBJECT_H_BANNER_BLOOD_ELF_CENTER, BG_EY_TEXT_HORDE_TAKEN_BLOOD_ELF_TOWER, EY_GRAVEYARD_BLOOD_ELF),
    BattlegroundEYCapturingPointStruct(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_CENTER, BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_CENTER, BG_EY_TEXT_ALLIANCE_TAKEN_DRAENEI_RUINS, BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_CENTER, BG_EY_TEXT_HORDE_TAKEN_DRAENEI_RUINS, EY_GRAVEYARD_DRAENEI_RUINS),
    BattlegroundEYCapturingPointStruct(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_CENTER, BG_EY_OBJECT_A_BANNER_MAGE_TOWER_CENTER, BG_EY_TEXT_ALLIANCE_TAKEN_MAGE_TOWER, BG_EY_OBJECT_H_BANNER_MAGE_TOWER_CENTER, BG_EY_TEXT_HORDE_TAKEN_MAGE_TOWER, EY_GRAVEYARD_MAGE_TOWER)
};

struct BattlegroundEYScore final : public BattlegroundScore
{
    friend class BattlegroundEY;

    protected:
        BattlegroundEYScore(ObjectGuid playerGuid) : BattlegroundScore(playerGuid), FlagCaptures(0) { }

        void UpdateScore(uint32 type, uint32 value) override
        {
            switch (type)
            {
                case SCORE_FLAG_CAPTURES:   // Flags captured
                    FlagCaptures += value;
                    break;
                default:
                    BattlegroundScore::UpdateScore(type, value);
                    break;
            }
        }

        void BuildObjectivesBlock(WorldPackets::Battleground::PVPLogData_Player& playerData) override;

        uint32 GetAttr1() const override { return FlagCaptures; }

        uint32 FlagCaptures;
};

class BattlegroundEY : public Battleground
{
    public:
        BattlegroundEY();
        ~BattlegroundEY();

        /* inherited from BattlegroundClass */
        void AddPlayer(Player* player) override;
        void StartingEventCloseDoors() override;
        void StartingEventOpenDoors() override;

        /* BG Flags */
        ObjectGuid GetFlagPickerGUID(int32 /*team*/ = -1) const override { return m_FlagKeeper; }
        void SetFlagPicker(ObjectGuid guid) { m_FlagKeeper = guid; }
        bool IsFlagPickedup() const         { return !m_FlagKeeper.IsEmpty(); }
        uint8 GetFlagState() const          { return m_FlagState; }
        void RespawnFlag(bool send_message);
        void RespawnFlagAfterDrop();

        void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;
        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;
        void HandleKillPlayer(Player* player, Player* killer) override;
        WorldSafeLocsEntry const* GetClosestGraveyard(Player* player) override;
        bool SetupBattleground() override;
        void Reset() override;
        void UpdateTeamScore(uint32 Team);
        void EndBattleground(uint32 winner) override;
        bool UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor = true) override;
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;
        void SetDroppedFlagGUID(ObjectGuid guid, int32 /*TeamID*/ = -1) override  { m_DroppedFlagGUID = guid; }
        ObjectGuid GetDroppedFlagGUID() const { return m_DroppedFlagGUID; }

        //npcbot
        void AddBot(Creature* bot) override;
        WorldSafeLocsEntry const* GetClosestGraveyardForBot(WorldLocation const& curPos, uint32 team) const override;
        void RemoveBot(ObjectGuid guid) override;
        bool UpdateBotScore(Creature const* bot, uint32 type, uint32 value, bool doAddHonor = true) override;
        void HandleBotKillPlayer(Creature* killer, Player* victim) override;
        void HandleBotKillBot(Creature* killer, Creature* victim) override;
        void HandlePlayerKillBot(Creature* victim, Player* killer) override;
        void EventBotDroppedFlag(Creature* bot) override;
        void EventBotClickedOnFlag(Creature* bot, GameObject* target_obj) override;
        void HandleBotAreaTrigger(Creature* bot, uint32 trigger) override;
        void EventBotCapturedFlag(Creature* bot, uint32 bgObjectType);
        void EventBotTeamCapturedPoint(Creature const* bot, uint32 Point);
        void EventBotTeamLostPoint(Creature const* bot, uint32 Point);
        GuidVector const& GetPlayersNearPoint(uint8 point) const { return m_PlayersNearPoint[point]; }
        TeamId GetPointOwner(uint8 point) const { return m_PointOwnedByTeam[point] == ALLIANCE ? TEAM_ALLIANCE : m_PointOwnedByTeam[point] == HORDE ? TEAM_HORDE : TEAM_NEUTRAL; }
        //end npcbot

        /* Battleground Events */
        void EventPlayerClickedOnFlag(Player* Source, GameObject* target_obj) override;
        void EventPlayerDroppedFlag(Player* Source) override;

        /* achievement req. */
        bool IsAllNodesControlledByTeam(uint32 team) const override;

        uint32 GetPrematureWinner() override;
    private:
        void PostUpdateImpl(uint32 diff) override;

        void EventPlayerCapturedFlag(Player* Source, uint32 BgObjectType);
        void EventTeamCapturedPoint(Player* Source, uint32 Point);
        void EventTeamLostPoint(Player* Source, uint32 Point);
        void UpdatePointsCount(uint32 Team);
        void UpdatePointsIcons(uint32 Team, uint32 Point);

        /* Point status updating procedures */
        void CheckSomeoneLeftPoint();
        void CheckSomeoneJoinedPoint();
        void UpdatePointStatuses();

        /* Scorekeeping */
        void AddPoints(uint32 Team, uint32 Points);

        void RemovePoint(uint32 TeamID, uint32 Points = 1) { m_TeamScores[GetTeamIndexByTeamId(TeamID)] -= Points; }
        void SetTeamPoint(uint32 TeamID, uint32 Points = 0) { m_TeamScores[GetTeamIndexByTeamId(TeamID)] = Points; }

        uint32 m_HonorScoreTics[2];
        uint32 m_TeamPointsCount[2];

        uint32 m_Points_Trigger[EY_POINTS_MAX];

        ObjectGuid m_FlagKeeper;                                // keepers guid
        ObjectGuid m_DroppedFlagGUID;
        uint32 m_FlagCapturedBgObjectType;                  // type that should be despawned when flag is captured
        uint8 m_FlagState;                                  // for checking flag state
        int32 m_FlagsTimer;
        int32 m_TowerCapCheckTimer;

        uint32 m_PointOwnedByTeam[EY_POINTS_MAX];
        uint8 m_PointState[EY_POINTS_MAX];
        int32 m_PointBarStatus[EY_POINTS_MAX];
        GuidVector m_PlayersNearPoint[EY_POINTS_MAX + 1];
        uint8 m_CurrentPointPlayersCount[2*EY_POINTS_MAX];

        int32 m_PointAddingTimer;
        uint32 m_HonorTics;
};
#endif
