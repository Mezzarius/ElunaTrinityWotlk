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

#include "BattlegroundWS.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldStatePackets.h"

//npcbot
#include "botdatamgr.h"
#include "botmgr.h"
//end npcbot

// these variables aren't used outside of this file, so declare them only here
enum BG_WSG_Rewards
{
    BG_WSG_WIN = 0,
    BG_WSG_FLAG_CAP,
    BG_WSG_MAP_COMPLETE,
    BG_WSG_REWARD_NUM
};

uint32 BG_WSG_Honor[BG_HONOR_MODE_NUM][BG_WSG_REWARD_NUM] =
{
    {20, 40, 40}, // normal honor
    {60, 40, 80}  // holiday
};

BattlegroundWS::BattlegroundWS()
{
    BgObjects.resize(BG_WS_OBJECT_MAX);
    BgCreatures.resize(BG_CREATURES_MAX_WS);

    StartMessageIds[BG_STARTING_EVENT_SECOND] = BG_WS_TEXT_START_ONE_MINUTE;
    StartMessageIds[BG_STARTING_EVENT_THIRD]  = BG_WS_TEXT_START_HALF_MINUTE;
    StartMessageIds[BG_STARTING_EVENT_FOURTH] = BG_WS_TEXT_BATTLE_HAS_BEGUN;

    _flagSpellForceTimer = 0;
    _bothFlagsKept = false;
    _flagDebuffState = 0;
    m_FlagKeepers[TEAM_ALLIANCE].Clear();
    m_FlagKeepers[TEAM_HORDE].Clear();
    m_DroppedFlagGUID[TEAM_ALLIANCE].Clear();
    m_DroppedFlagGUID[TEAM_HORDE].Clear();
    _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_BASE;
    _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_ON_BASE;
    _flagsTimer[TEAM_ALLIANCE] = 0;
    _flagsTimer[TEAM_HORDE] = 0;
    _flagsDropTimer[TEAM_ALLIANCE] = 0;
    _flagsDropTimer[TEAM_HORDE] = 0;
    _lastFlagCaptureTeam = 0;
    m_ReputationCapture = 0;
    m_HonorWinKills = 0;
    m_HonorEndKills = 0;
    _minutesElapsed = 0;
}

void BattlegroundWGScore::BuildObjectivesBlock(WorldPackets::Battleground::PVPLogData_Player& playerData)
{
    playerData.Stats = { FlagCaptures, FlagReturns };
}

BattlegroundWS::~BattlegroundWS() { }

void BattlegroundWS::PostUpdateImpl(uint32 diff)
{
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        if (GetStartTime() >= 27*MINUTE*IN_MILLISECONDS)
        {
            if (GetTeamScore(TEAM_ALLIANCE) == 0)
            {
                if (GetTeamScore(TEAM_HORDE) == 0)        // No one scored - result is tie
                    EndBattleground(0);
                else                                 // Horde has more points and thus wins
                    EndBattleground(HORDE);
            }
            else if (GetTeamScore(TEAM_HORDE) == 0)
                EndBattleground(ALLIANCE);           // Alliance has > 0, Horde has 0, alliance wins
            else if (GetTeamScore(TEAM_HORDE) == GetTeamScore(TEAM_ALLIANCE)) // Team score equal, winner is team that scored the last flag
                EndBattleground(_lastFlagCaptureTeam);
            else if (GetTeamScore(TEAM_HORDE) > GetTeamScore(TEAM_ALLIANCE))  // Last but not least, check who has the higher score
                EndBattleground(HORDE);
            else
                EndBattleground(ALLIANCE);
        }
        // first update needed after 1 minute of game already in progress
        else if (GetStartTime() > uint32(_minutesElapsed * MINUTE * IN_MILLISECONDS) +  3 * MINUTE * IN_MILLISECONDS)
        {
            ++_minutesElapsed;
            UpdateWorldState(BG_WS_STATE_TIMER, 25 - _minutesElapsed);
        }

        if (_flagState[TEAM_ALLIANCE] == BG_WS_FLAG_STATE_WAIT_RESPAWN)
        {
            _flagsTimer[TEAM_ALLIANCE] -= diff;

            if (_flagsTimer[TEAM_ALLIANCE] < 0)
            {
                _flagsTimer[TEAM_ALLIANCE] = 0;
                RespawnFlag(ALLIANCE, true);
            }
        }

        if (_flagState[TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_GROUND)
        {
            _flagsDropTimer[TEAM_ALLIANCE] -= diff;

            if (_flagsDropTimer[TEAM_ALLIANCE] < 0)
            {
                _flagsDropTimer[TEAM_ALLIANCE] = 0;
                RespawnFlagAfterDrop(ALLIANCE);
                _bothFlagsKept = false;
            }
        }

        if (_flagState[TEAM_HORDE] == BG_WS_FLAG_STATE_WAIT_RESPAWN)
        {
            _flagsTimer[TEAM_HORDE] -= diff;

            if (_flagsTimer[TEAM_HORDE] < 0)
            {
                _flagsTimer[TEAM_HORDE] = 0;
                RespawnFlag(HORDE, true);
            }
        }

        if (_flagState[TEAM_HORDE] == BG_WS_FLAG_STATE_ON_GROUND)
        {
            _flagsDropTimer[TEAM_HORDE] -= diff;

            if (_flagsDropTimer[TEAM_HORDE] < 0)
            {
                _flagsDropTimer[TEAM_HORDE] = 0;
                RespawnFlagAfterDrop(HORDE);
                _bothFlagsKept = false;
            }
        }

        if (_bothFlagsKept)
        {
            _flagSpellForceTimer += diff;
            if (_flagDebuffState == 0 && _flagSpellForceTimer >= 10*MINUTE*IN_MILLISECONDS)  //10 minutes
            {
                // Apply Stage 1 (Focused Assault)
                if (Player* player = ObjectAccessor::FindPlayer(m_FlagKeepers[0]))
                    player->CastSpell(player, WS_SPELL_FOCUSED_ASSAULT, true);
                if (Player* player = ObjectAccessor::FindPlayer(m_FlagKeepers[1]))
                    player->CastSpell(player, WS_SPELL_FOCUSED_ASSAULT, true);
                _flagDebuffState = 1;
            }
            else if (_flagDebuffState == 1 && _flagSpellForceTimer >= 15*MINUTE*IN_MILLISECONDS) //15 minutes
            {
                // Apply Stage 2 (Brutal Assault)
                if (Player* player = ObjectAccessor::FindPlayer(m_FlagKeepers[0]))
                {
                    player->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
                    player->CastSpell(player, WS_SPELL_BRUTAL_ASSAULT, true);
                }
                if (Player* player = ObjectAccessor::FindPlayer(m_FlagKeepers[1]))
                {
                    player->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
                    player->CastSpell(player, WS_SPELL_BRUTAL_ASSAULT, true);
                }
                _flagDebuffState = 2;
            }
        }
        else if ((_flagState[TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_BASE || _flagState[TEAM_ALLIANCE] == BG_WS_FLAG_STATE_WAIT_RESPAWN) &&
         (_flagState[TEAM_HORDE] == BG_WS_FLAG_STATE_ON_BASE || _flagState[TEAM_HORDE] == BG_WS_FLAG_STATE_WAIT_RESPAWN))
        {
            // Both flags are in base or awaiting respawn.
            // Remove assault debuffs, reset timers

            if (Player* player = ObjectAccessor::FindPlayer(m_FlagKeepers[0]))
            {
                player->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
                player->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);
            }
            if (Player* player = ObjectAccessor::FindPlayer(m_FlagKeepers[1]))
            {
                player->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
                player->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);
            }

            _flagSpellForceTimer = 0; //reset timer.
            _flagDebuffState = 0;
        }
    }
}

void BattlegroundWS::StartingEventCloseDoors()
{
    for (uint32 i = BG_WS_OBJECT_DOOR_A_1; i <= BG_WS_OBJECT_DOOR_H_4; ++i)
    {
        DoorClose(i);
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    }
    for (uint32 i = BG_WS_OBJECT_A_FLAG; i <= BG_WS_OBJECT_BERSERKBUFF_2; ++i)
        SpawnBGObject(i, RESPAWN_ONE_DAY);

    UpdateWorldState(BG_WS_STATE_TIMER_ACTIVE, 1);
    UpdateWorldState(BG_WS_STATE_TIMER, 25);
}

void BattlegroundWS::StartingEventOpenDoors()
{
    for (uint32 i = BG_WS_OBJECT_DOOR_A_1; i <= BG_WS_OBJECT_DOOR_A_6; ++i)
        DoorOpen(i);
    for (uint32 i = BG_WS_OBJECT_DOOR_H_1; i <= BG_WS_OBJECT_DOOR_H_4; ++i)
        DoorOpen(i);

    for (uint32 i = BG_WS_OBJECT_A_FLAG; i <= BG_WS_OBJECT_BERSERKBUFF_2; ++i)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);

    SpawnBGObject(BG_WS_OBJECT_DOOR_A_5, RESPAWN_ONE_DAY);
    SpawnBGObject(BG_WS_OBJECT_DOOR_A_6, RESPAWN_ONE_DAY);
    SpawnBGObject(BG_WS_OBJECT_DOOR_H_3, RESPAWN_ONE_DAY);
    SpawnBGObject(BG_WS_OBJECT_DOOR_H_4, RESPAWN_ONE_DAY);

    // players joining later are not eligibles
    StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, WS_EVENT_START_BATTLE);
}

void BattlegroundWS::AddPlayer(Player* player)
{
    bool const isInBattleground = IsPlayerInBattleground(player->GetGUID());
    Battleground::AddPlayer(player);
    if (!isInBattleground)
        PlayerScores[player->GetGUID().GetCounter()] = new BattlegroundWGScore(player->GetGUID());
}

//npcbot
void BattlegroundWS::AddBot(Creature* bot)
{
    bool const isInBattleground = IsPlayerInBattleground(bot->GetGUID());
    Battleground::AddBot(bot);
    if (!isInBattleground)
        BotScores[bot->GetEntry()] = new BattlegroundWGScore(bot->GetGUID());
}
//end npcbot

void BattlegroundWS::RespawnFlag(uint32 Team, bool captured)
{
    if (Team == ALLIANCE)
    {
        TC_LOG_DEBUG("bg.battleground", "Respawn Alliance flag");
        _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_BASE;
    }
    else
    {
        TC_LOG_DEBUG("bg.battleground", "Respawn Horde flag");
        _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_ON_BASE;
    }

    if (captured)
    {
        //when map_update will be allowed for battlegrounds this code will be useless
        SpawnBGObject(BG_WS_OBJECT_H_FLAG, RESPAWN_IMMEDIATELY);
        SpawnBGObject(BG_WS_OBJECT_A_FLAG, RESPAWN_IMMEDIATELY);
        SendBroadcastText(BG_WS_TEXT_FLAGS_PLACED, CHAT_MSG_BG_SYSTEM_NEUTRAL);
        PlaySoundToAll(BG_WS_SOUND_FLAGS_RESPAWNED);        // flag respawned sound...
    }
    _bothFlagsKept = false;
}

void BattlegroundWS::RespawnFlagAfterDrop(uint32 team)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    RespawnFlag(team, false);
    if (team == ALLIANCE)
        SpawnBGObject(BG_WS_OBJECT_A_FLAG, RESPAWN_IMMEDIATELY);
    else
        SpawnBGObject(BG_WS_OBJECT_H_FLAG, RESPAWN_IMMEDIATELY);

    SendBroadcastText(BG_WS_TEXT_FLAGS_PLACED, CHAT_MSG_BG_SYSTEM_NEUTRAL);
    PlaySoundToAll(BG_WS_SOUND_FLAGS_RESPAWNED);

    if (GameObject* obj = GetBgMap()->GetGameObject(GetDroppedFlagGUID(team)))
        obj->Delete();
    else
        TC_LOG_ERROR("bg.battleground", "unknown dropped flag ({})", GetDroppedFlagGUID(team).ToString());

    SetDroppedFlagGUID(ObjectGuid::Empty, GetTeamIndexByTeamId(team));
    _bothFlagsKept = false;
    // Check opposing flag if it is in capture zone; if so, capture it
    HandleFlagRoomCapturePoint(team == ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE);
}

void BattlegroundWS::EventPlayerCapturedFlag(Player* player)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    uint32 winner = 0;

    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
    if (player->GetTeam() == ALLIANCE)
    {
        if (!IsHordeFlagPickedup())
            return;
        SetHordeFlagPicker(ObjectGuid::Empty);              // must be before aura remove to prevent 2 events (drop+capture) at the same time
                                                            // horde flag in base (but not respawned yet)
        _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_WAIT_RESPAWN;
                                                            // Drop Horde Flag from Player
        player->RemoveAurasDueToSpell(BG_WS_SPELL_WARSONG_FLAG);
        if (_flagDebuffState == 1)
          player->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
        else if (_flagDebuffState == 2)
          player->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);

        if (GetTeamScore(TEAM_ALLIANCE) < BG_WS_MAX_TEAM_SCORE)
            AddPoint(ALLIANCE, 1);
        PlaySoundToAll(BG_WS_SOUND_FLAG_CAPTURED_ALLIANCE);
        RewardReputationToTeam(890, m_ReputationCapture, ALLIANCE);
    }
    else
    {
        if (!IsAllianceFlagPickedup())
            return;
        SetAllianceFlagPicker(ObjectGuid::Empty);           // must be before aura remove to prevent 2 events (drop+capture) at the same time
                                                            // alliance flag in base (but not respawned yet)
        _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_WAIT_RESPAWN;
                                                            // Drop Alliance Flag from Player
        player->RemoveAurasDueToSpell(BG_WS_SPELL_SILVERWING_FLAG);
        if (_flagDebuffState == 1)
          player->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
        else if (_flagDebuffState == 2)
          player->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);

        if (GetTeamScore(TEAM_HORDE) < BG_WS_MAX_TEAM_SCORE)
            AddPoint(HORDE, 1);
        PlaySoundToAll(BG_WS_SOUND_FLAG_CAPTURED_HORDE);
        RewardReputationToTeam(889, m_ReputationCapture, HORDE);
    }
    //for flag capture is reward 2 honorable kills
    RewardHonorToTeam(GetBonusHonorFromKill(2), player->GetTeam());

    SpawnBGObject(BG_WS_OBJECT_H_FLAG, BG_WS_FLAG_RESPAWN_TIME);
    SpawnBGObject(BG_WS_OBJECT_A_FLAG, BG_WS_FLAG_RESPAWN_TIME);

    if (player->GetTeam() == ALLIANCE)
        SendBroadcastText(BG_WS_TEXT_CAPTURED_HORDE_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
    else
        SendBroadcastText(BG_WS_TEXT_CAPTURED_ALLIANCE_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, player);

    UpdateFlagState(player->GetTeam(), 1);                  // flag state none
    UpdateTeamScore(player->GetTeamId());
    // only flag capture should be updated
    UpdatePlayerScore(player, SCORE_FLAG_CAPTURES, 1);      // +1 flag captures

    // update last flag capture to be used if teamscore is equal
    SetLastFlagCapture(player->GetTeam());

    if (GetTeamScore(TEAM_ALLIANCE) == BG_WS_MAX_TEAM_SCORE)
        winner = ALLIANCE;

    if (GetTeamScore(TEAM_HORDE) == BG_WS_MAX_TEAM_SCORE)
        winner = HORDE;

    if (winner)
    {
        UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 0);
        UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 0);
        UpdateWorldState(BG_WS_FLAG_STATE_ALLIANCE, 1);
        UpdateWorldState(BG_WS_FLAG_STATE_HORDE, 1);
        UpdateWorldState(BG_WS_STATE_TIMER_ACTIVE, 0);

        RewardHonorToTeam(BG_WSG_Honor[m_HonorMode][BG_WSG_WIN], winner);
        EndBattleground(winner);
    }
    else
    {
        _flagsTimer[GetTeamIndexByTeamId(player->GetTeam()) ? 0 : 1] = BG_WS_FLAG_RESPAWN_TIME;
    }
}
void BattlegroundWS::HandleFlagRoomCapturePoint(int32 team)
{
    //npcbot
    if (GetFlagPickerGUID(team).IsCreature())
    {
        Creature const* flagBotCarrier = BotDataMgr::FindBot(GetFlagPickerGUID(team).GetEntry());
        uint32 areaBotTrigger = team == TEAM_ALLIANCE ? 3647 : 3646;
        if (flagBotCarrier && BotMgr::IsBotInAreaTriggerRadius(flagBotCarrier, sAreaTriggerStore.LookupEntry(areaBotTrigger)))
            EventBotCapturedFlag(const_cast<Creature*>(flagBotCarrier));
        return;
    }
    //end npcbot

    Player* flagCarrier = ObjectAccessor::GetPlayer(GetBgMap(), GetFlagPickerGUID(team));
    uint32 areaTrigger = team == TEAM_ALLIANCE ? 3647 : 3646;
    if (flagCarrier && flagCarrier->IsInAreaTriggerRadius(sAreaTriggerStore.LookupEntry(areaTrigger)))
        EventPlayerCapturedFlag(flagCarrier);
}

//npcbot
void BattlegroundWS::EventBotCapturedFlag(Creature* bot)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    uint32 winner = 0;

    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
    if (GetBotTeam(bot->GetGUID()) == ALLIANCE)
    {
        if (!IsHordeFlagPickedup())
            return;
        SetHordeFlagPicker(ObjectGuid::Empty);              // must be before aura remove to prevent 2 events (drop+capture) at the same time
                                                            // horde flag in base (but not respawned yet)
        _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_WAIT_RESPAWN;
                                                            // Drop Horde Flag from Player
        bot->RemoveAurasDueToSpell(BG_WS_SPELL_WARSONG_FLAG);
        if (_flagDebuffState == 1)
          bot->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
        else if (_flagDebuffState == 2)
          bot->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);

        if (GetTeamScore(TEAM_ALLIANCE) < BG_WS_MAX_TEAM_SCORE)
            AddPoint(ALLIANCE, 1);
        PlaySoundToAll(BG_WS_SOUND_FLAG_CAPTURED_ALLIANCE);
        RewardReputationToTeam(890, m_ReputationCapture, ALLIANCE);
    }
    else
    {
        if (!IsAllianceFlagPickedup())
            return;
        SetAllianceFlagPicker(ObjectGuid::Empty);           // must be before aura remove to prevent 2 events (drop+capture) at the same time
                                                            // alliance flag in base (but not respawned yet)
        _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_WAIT_RESPAWN;
                                                            // Drop Alliance Flag from Player
        bot->RemoveAurasDueToSpell(BG_WS_SPELL_SILVERWING_FLAG);
        if (_flagDebuffState == 1)
          bot->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
        else if (_flagDebuffState == 2)
          bot->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);

        if (GetTeamScore(TEAM_HORDE) < BG_WS_MAX_TEAM_SCORE)
            AddPoint(HORDE, 1);
        PlaySoundToAll(BG_WS_SOUND_FLAG_CAPTURED_HORDE);
        RewardReputationToTeam(889, m_ReputationCapture, HORDE);
    }
    //for flag capture is reward 2 honorable kills
    RewardHonorToTeam(GetBonusHonorFromKill(2), GetBotTeam(bot->GetGUID()));

    SpawnBGObject(BG_WS_OBJECT_H_FLAG, BG_WS_FLAG_RESPAWN_TIME);
    SpawnBGObject(BG_WS_OBJECT_A_FLAG, BG_WS_FLAG_RESPAWN_TIME);

    if (GetBotTeam(bot->GetGUID()) == ALLIANCE)
        SendBroadcastText(BG_WS_TEXT_CAPTURED_HORDE_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
    else
        SendBroadcastText(BG_WS_TEXT_CAPTURED_ALLIANCE_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, bot);

    UpdateFlagState(GetBotTeam(bot->GetGUID()), 1);                  // flag state none
    UpdateTeamScore(GetBotTeamId(bot->GetGUID()));
    // only flag capture should be updated
    UpdateBotScore(bot, SCORE_FLAG_CAPTURES, 1);      // +1 flag captures

    // update last flag capture to be used if teamscore is equal
    SetLastFlagCapture(GetBotTeam(bot->GetGUID()));

    if (GetTeamScore(TEAM_ALLIANCE) == BG_WS_MAX_TEAM_SCORE)
        winner = ALLIANCE;

    if (GetTeamScore(TEAM_HORDE) == BG_WS_MAX_TEAM_SCORE)
        winner = HORDE;

    if (winner)
    {
        UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 0);
        UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 0);
        UpdateWorldState(BG_WS_FLAG_STATE_ALLIANCE, 1);
        UpdateWorldState(BG_WS_FLAG_STATE_HORDE, 1);
        UpdateWorldState(BG_WS_STATE_TIMER_ACTIVE, 0);

        RewardHonorToTeam(BG_WSG_Honor[m_HonorMode][BG_WSG_WIN], winner);
        EndBattleground(winner);
    }
    else
    {
        _flagsTimer[GetTeamIndexByTeamId(GetBotTeam(bot->GetGUID())) ? 0 : 1] = BG_WS_FLAG_RESPAWN_TIME;
    }
}
//end npcbot

void BattlegroundWS::EventPlayerDroppedFlag(Player* player)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
    {
        // if not running, do not cast things at the dropper player (prevent spawning the "dropped" flag), neither send unnecessary messages
        // just take off the aura
        if (player->GetTeam() == ALLIANCE)
        {
            if (!IsHordeFlagPickedup())
                return;

            if (GetFlagPickerGUID(TEAM_HORDE) == player->GetGUID())
            {
                SetHordeFlagPicker(ObjectGuid::Empty);
                player->RemoveAurasDueToSpell(BG_WS_SPELL_WARSONG_FLAG);
            }
        }
        else
        {
            if (!IsAllianceFlagPickedup())
                return;

            if (GetFlagPickerGUID(TEAM_ALLIANCE) == player->GetGUID())
            {
                SetAllianceFlagPicker(ObjectGuid::Empty);
                player->RemoveAurasDueToSpell(BG_WS_SPELL_SILVERWING_FLAG);
            }
        }
        return;
    }

    bool set = false;

    if (player->GetTeam() == ALLIANCE)
    {
        if (!IsHordeFlagPickedup())
            return;
        if (GetFlagPickerGUID(TEAM_HORDE) == player->GetGUID())
        {
            SetHordeFlagPicker(ObjectGuid::Empty);
            player->RemoveAurasDueToSpell(BG_WS_SPELL_WARSONG_FLAG);
            if (_flagDebuffState == 1)
              player->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
            else if (_flagDebuffState == 2)
              player->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);
            _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_ON_GROUND;
            player->CastSpell(player, BG_WS_SPELL_WARSONG_FLAG_DROPPED, true);
            set = true;
        }
    }
    else
    {
        if (!IsAllianceFlagPickedup())
            return;
        if (GetFlagPickerGUID(TEAM_ALLIANCE) == player->GetGUID())
        {
            SetAllianceFlagPicker(ObjectGuid::Empty);
            player->RemoveAurasDueToSpell(BG_WS_SPELL_SILVERWING_FLAG);
            if (_flagDebuffState == 1)
              player->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
            else if (_flagDebuffState == 2)
              player->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);
            _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_GROUND;
            player->CastSpell(player, BG_WS_SPELL_SILVERWING_FLAG_DROPPED, true);
            set = true;
        }
    }

    if (set)
    {
        player->CastSpell(player, SPELL_RECENTLY_DROPPED_FLAG, true);
        UpdateFlagState(player->GetTeam(), 1);

        if (player->GetTeam() == ALLIANCE)
        {
            SendBroadcastText(BG_WS_TEXT_HORDE_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_HORDE, player);
            UpdateWorldState(BG_WS_FLAG_UNK_HORDE, uint32(-1));
        }
        else
        {
            SendBroadcastText(BG_WS_TEXT_ALLIANCE_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
            UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, uint32(-1));
        }

        _flagsDropTimer[GetTeamIndexByTeamId(player->GetTeam()) ? 0 : 1] = BG_WS_FLAG_DROP_TIME;
    }
}

//npcbot
void BattlegroundWS::EventBotDroppedFlag(Creature* bot)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
    {
        // if not running, do not cast things at the dropper player (prevent spawning the "dropped" flag), neither send unnecessary messages
        // just take off the aura
        if (GetBotTeam(bot->GetGUID()) == ALLIANCE)
        {
            if (!IsHordeFlagPickedup())
                return;

            if (GetFlagPickerGUID(TEAM_HORDE) == bot->GetGUID())
            {
                SetHordeFlagPicker(ObjectGuid::Empty);
                bot->RemoveAurasDueToSpell(BG_WS_SPELL_WARSONG_FLAG);
            }
        }
        else
        {
            if (!IsAllianceFlagPickedup())
                return;

            if (GetFlagPickerGUID(TEAM_ALLIANCE) == bot->GetGUID())
            {
                SetAllianceFlagPicker(ObjectGuid::Empty);
                bot->RemoveAurasDueToSpell(BG_WS_SPELL_SILVERWING_FLAG);
            }
        }
        return;
    }

    bool set = false;

    if (GetBotTeam(bot->GetGUID()) == ALLIANCE)
    {
        if (!IsHordeFlagPickedup())
            return;
        if (GetFlagPickerGUID(TEAM_HORDE) == bot->GetGUID())
        {
            SetHordeFlagPicker(ObjectGuid::Empty);
            bot->RemoveAurasDueToSpell(BG_WS_SPELL_WARSONG_FLAG);
            if (_flagDebuffState == 1)
              bot->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
            else if (_flagDebuffState == 2)
              bot->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);
            _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_ON_GROUND;
            bot->CastSpell(bot, BG_WS_SPELL_WARSONG_FLAG_DROPPED, true);
            set = true;
        }
    }
    else
    {
        if (!IsAllianceFlagPickedup())
            return;
        if (GetFlagPickerGUID(TEAM_ALLIANCE) == bot->GetGUID())
        {
            SetAllianceFlagPicker(ObjectGuid::Empty);
            bot->RemoveAurasDueToSpell(BG_WS_SPELL_SILVERWING_FLAG);
            if (_flagDebuffState == 1)
              bot->RemoveAurasDueToSpell(WS_SPELL_FOCUSED_ASSAULT);
            else if (_flagDebuffState == 2)
              bot->RemoveAurasDueToSpell(WS_SPELL_BRUTAL_ASSAULT);
            _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_GROUND;
            bot->CastSpell(bot, BG_WS_SPELL_SILVERWING_FLAG_DROPPED, true);
            set = true;
        }
    }

    if (set)
    {
        bot->CastSpell(bot, SPELL_RECENTLY_DROPPED_FLAG, true);
        UpdateFlagState(GetBotTeam(bot->GetGUID()), 1);

        if (GetBotTeam(bot->GetGUID()) == ALLIANCE)
        {
            SendBroadcastText(BG_WS_TEXT_HORDE_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_HORDE, bot);
            UpdateWorldState(BG_WS_FLAG_UNK_HORDE, uint32(-1));
        }
        else
        {
            SendBroadcastText(BG_WS_TEXT_ALLIANCE_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
            UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, uint32(-1));
        }

        _flagsDropTimer[GetTeamIndexByTeamId(GetBotTeam(bot->GetGUID())) ? 0 : 1] = BG_WS_FLAG_DROP_TIME;
    }
}
//end npcbot

void BattlegroundWS::EventPlayerClickedOnFlag(Player* player, GameObject* target_obj)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    //alliance flag picked up from base
    if (player->GetTeam() == HORDE && GetFlagState(ALLIANCE) == BG_WS_FLAG_STATE_ON_BASE
        && BgObjects[BG_WS_OBJECT_A_FLAG] == target_obj->GetGUID())
    {
        SendBroadcastText(BG_WS_TEXT_ALLIANCE_FLAG_PICKED_UP, CHAT_MSG_BG_SYSTEM_HORDE, player);
        PlaySoundToAll(BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP);
        SpawnBGObject(BG_WS_OBJECT_A_FLAG, RESPAWN_ONE_DAY);
        SetAllianceFlagPicker(player->GetGUID());
        _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_PLAYER;
        //update world state to show correct flag carrier
        UpdateFlagState(HORDE, BG_WS_FLAG_STATE_ON_PLAYER);
        UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 1);
        player->CastSpell(player, BG_WS_SPELL_SILVERWING_FLAG, true);
        player->StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_SPELL_TARGET, BG_WS_SPELL_SILVERWING_FLAG_PICKED);
        if (_flagState[1] == BG_WS_FLAG_STATE_ON_PLAYER)
          _bothFlagsKept = true;

        if (_flagDebuffState == 1)
          player->CastSpell(player, WS_SPELL_FOCUSED_ASSAULT, true);
        else if (_flagDebuffState == 2)
          player->CastSpell(player, WS_SPELL_BRUTAL_ASSAULT, true);
    }

    //horde flag picked up from base
    if (player->GetTeam() == ALLIANCE && GetFlagState(HORDE) == BG_WS_FLAG_STATE_ON_BASE
        && BgObjects[BG_WS_OBJECT_H_FLAG] == target_obj->GetGUID())
    {
        SendBroadcastText(BG_WS_TEXT_HORDE_FLAG_PICKED_UP, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
        PlaySoundToAll(BG_WS_SOUND_HORDE_FLAG_PICKED_UP);
        SpawnBGObject(BG_WS_OBJECT_H_FLAG, RESPAWN_ONE_DAY);
        SetHordeFlagPicker(player->GetGUID());
        _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_ON_PLAYER;
        //update world state to show correct flag carrier
        UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_ON_PLAYER);
        UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 1);
        player->CastSpell(player, BG_WS_SPELL_WARSONG_FLAG, true);
        player->StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_SPELL_TARGET, BG_WS_SPELL_WARSONG_FLAG_PICKED);
        if (_flagState[0] == BG_WS_FLAG_STATE_ON_PLAYER)
          _bothFlagsKept = true;

        if (_flagDebuffState == 1)
          player->CastSpell(player, WS_SPELL_FOCUSED_ASSAULT, true);
        else if (_flagDebuffState == 2)
          player->CastSpell(player, WS_SPELL_BRUTAL_ASSAULT, true);
    }

    //Alliance flag on ground(not in base) (returned or picked up again from ground!)
    if (GetFlagState(ALLIANCE) == BG_WS_FLAG_STATE_ON_GROUND && player->IsWithinDistInMap(target_obj, 10)
        && target_obj->GetGOInfo()->entry == BG_OBJECT_A_FLAG_GROUND_WS_ENTRY)
    {
        if (player->GetTeam() == ALLIANCE)
        {
            SendBroadcastText(BG_WS_TEXT_ALLIANCE_FLAG_RETURNED, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
            UpdateFlagState(HORDE, BG_WS_FLAG_STATE_WAIT_RESPAWN);
            RespawnFlag(ALLIANCE, false);
            SpawnBGObject(BG_WS_OBJECT_A_FLAG, RESPAWN_IMMEDIATELY);
            PlaySoundToAll(BG_WS_SOUND_FLAG_RETURNED);
            UpdatePlayerScore(player, SCORE_FLAG_RETURNS, 1);
            _bothFlagsKept = false;
            HandleFlagRoomCapturePoint(TEAM_HORDE); // Check Horde flag if it is in capture zone; if so, capture it
        }
        else
        {
            SendBroadcastText(BG_WS_TEXT_ALLIANCE_FLAG_PICKED_UP, CHAT_MSG_BG_SYSTEM_HORDE, player);
            PlaySoundToAll(BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP);
            SpawnBGObject(BG_WS_OBJECT_A_FLAG, RESPAWN_ONE_DAY);
            SetAllianceFlagPicker(player->GetGUID());
            player->CastSpell(player, BG_WS_SPELL_SILVERWING_FLAG, true);
            _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_PLAYER;
            UpdateFlagState(HORDE, BG_WS_FLAG_STATE_ON_PLAYER);
            if (_flagDebuffState == 1)
              player->CastSpell(player, WS_SPELL_FOCUSED_ASSAULT, true);
            else if (_flagDebuffState == 2)
              player->CastSpell(player, WS_SPELL_BRUTAL_ASSAULT, true);
            UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 1);
        }
        //called in HandleGameObjectUseOpcode:
        //target_obj->Delete();
    }

    //Horde flag on ground(not in base) (returned or picked up again)
    if (GetFlagState(HORDE) == BG_WS_FLAG_STATE_ON_GROUND && player->IsWithinDistInMap(target_obj, 10)
        && target_obj->GetGOInfo()->entry == BG_OBJECT_H_FLAG_GROUND_WS_ENTRY)
    {
        if (player->GetTeam() == HORDE)
        {
            SendBroadcastText(BG_WS_TEXT_HORDE_FLAG_RETURNED, CHAT_MSG_BG_SYSTEM_HORDE, player);
            UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_WAIT_RESPAWN);
            RespawnFlag(HORDE, false);
            SpawnBGObject(BG_WS_OBJECT_H_FLAG, RESPAWN_IMMEDIATELY);
            PlaySoundToAll(BG_WS_SOUND_FLAG_RETURNED);
            UpdatePlayerScore(player, SCORE_FLAG_RETURNS, 1);
            _bothFlagsKept = false;
            HandleFlagRoomCapturePoint(TEAM_ALLIANCE); // Check Alliance flag if it is in capture zone; if so, capture it
        }
        else
        {
            SendBroadcastText(BG_WS_TEXT_HORDE_FLAG_PICKED_UP, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
            PlaySoundToAll(BG_WS_SOUND_HORDE_FLAG_PICKED_UP);
            SpawnBGObject(BG_WS_OBJECT_H_FLAG, RESPAWN_ONE_DAY);
            SetHordeFlagPicker(player->GetGUID());
            player->CastSpell(player, BG_WS_SPELL_WARSONG_FLAG, true);
            _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_ON_PLAYER;
            UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_ON_PLAYER);
            if (_flagDebuffState == 1)
              player->CastSpell(player, WS_SPELL_FOCUSED_ASSAULT, true);
            else if (_flagDebuffState == 2)
              player->CastSpell(player, WS_SPELL_BRUTAL_ASSAULT, true);
            UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 1);
        }
        //called in HandleGameObjectUseOpcode:
        //target_obj->Delete();
    }

    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
}

//npcbot
void BattlegroundWS::EventBotClickedOnFlag(Creature* bot, GameObject* target_obj)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    //alliance flag picked up from base
    if (GetBotTeam(bot->GetGUID()) == HORDE && GetFlagState(ALLIANCE) == BG_WS_FLAG_STATE_ON_BASE
        && BgObjects[BG_WS_OBJECT_A_FLAG] == target_obj->GetGUID())
    {
        SendBroadcastText(BG_WS_TEXT_ALLIANCE_FLAG_PICKED_UP, CHAT_MSG_BG_SYSTEM_HORDE, bot);
        PlaySoundToAll(BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP);
        SpawnBGObject(BG_WS_OBJECT_A_FLAG, RESPAWN_ONE_DAY);
        SetAllianceFlagPicker(bot->GetGUID());
        _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_PLAYER;
        //update world state to show correct flag carrier
        UpdateFlagState(HORDE, BG_WS_FLAG_STATE_ON_PLAYER);
        UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 1);
        bot->CastSpell(bot, BG_WS_SPELL_SILVERWING_FLAG, true);
        if (_flagState[1] == BG_WS_FLAG_STATE_ON_PLAYER)
          _bothFlagsKept = true;

        if (_flagDebuffState == 1)
          bot->CastSpell(bot, WS_SPELL_FOCUSED_ASSAULT, true);
        else if (_flagDebuffState == 2)
          bot->CastSpell(bot, WS_SPELL_BRUTAL_ASSAULT, true);
    }

    //horde flag picked up from base
    if (GetBotTeam(bot->GetGUID()) == ALLIANCE && GetFlagState(HORDE) == BG_WS_FLAG_STATE_ON_BASE
        && BgObjects[BG_WS_OBJECT_H_FLAG] == target_obj->GetGUID())
    {
        SendBroadcastText(BG_WS_TEXT_HORDE_FLAG_PICKED_UP, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
        PlaySoundToAll(BG_WS_SOUND_HORDE_FLAG_PICKED_UP);
        SpawnBGObject(BG_WS_OBJECT_H_FLAG, RESPAWN_ONE_DAY);
        SetHordeFlagPicker(bot->GetGUID());
        _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_ON_PLAYER;
        //update world state to show correct flag carrier
        UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_ON_PLAYER);
        UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 1);
        bot->CastSpell(bot, BG_WS_SPELL_WARSONG_FLAG, true);
        if (_flagState[0] == BG_WS_FLAG_STATE_ON_PLAYER)
          _bothFlagsKept = true;

        if (_flagDebuffState == 1)
          bot->CastSpell(bot, WS_SPELL_FOCUSED_ASSAULT, true);
        else if (_flagDebuffState == 2)
          bot->CastSpell(bot, WS_SPELL_BRUTAL_ASSAULT, true);
    }

    //Alliance flag on ground(not in base) (returned or picked up again from ground!)
    if (GetFlagState(ALLIANCE) == BG_WS_FLAG_STATE_ON_GROUND && bot->IsWithinDistInMap(target_obj, 10)
        && target_obj->GetGOInfo()->entry == BG_OBJECT_A_FLAG_GROUND_WS_ENTRY)
    {
        if (GetBotTeam(bot->GetGUID()) == ALLIANCE)
        {
            SendBroadcastText(BG_WS_TEXT_ALLIANCE_FLAG_RETURNED, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
            UpdateFlagState(HORDE, BG_WS_FLAG_STATE_WAIT_RESPAWN);
            RespawnFlag(ALLIANCE, false);
            SpawnBGObject(BG_WS_OBJECT_A_FLAG, RESPAWN_IMMEDIATELY);
            PlaySoundToAll(BG_WS_SOUND_FLAG_RETURNED);
            UpdateBotScore(bot, SCORE_FLAG_RETURNS, 1);
            _bothFlagsKept = false;
            HandleFlagRoomCapturePoint(TEAM_HORDE); // Check Horde flag if it is in capture zone; if so, capture it
        }
        else
        {
            SendBroadcastText(BG_WS_TEXT_ALLIANCE_FLAG_PICKED_UP, CHAT_MSG_BG_SYSTEM_HORDE, bot);
            PlaySoundToAll(BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP);
            SpawnBGObject(BG_WS_OBJECT_A_FLAG, RESPAWN_ONE_DAY);
            SetAllianceFlagPicker(bot->GetGUID());
            bot->CastSpell(bot, BG_WS_SPELL_SILVERWING_FLAG, true);
            _flagState[TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_PLAYER;
            UpdateFlagState(HORDE, BG_WS_FLAG_STATE_ON_PLAYER);
            if (_flagDebuffState == 1)
              bot->CastSpell(bot, WS_SPELL_FOCUSED_ASSAULT, true);
            else if (_flagDebuffState == 2)
              bot->CastSpell(bot, WS_SPELL_BRUTAL_ASSAULT, true);
            UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 1);
        }
        //called in HandleGameObjectUseOpcode:
        target_obj->Delete();
    }

    //Horde flag on ground(not in base) (returned or picked up again)
    if (GetFlagState(HORDE) == BG_WS_FLAG_STATE_ON_GROUND && bot->IsWithinDistInMap(target_obj, 10)
        && target_obj->GetGOInfo()->entry == BG_OBJECT_H_FLAG_GROUND_WS_ENTRY)
    {
        if (GetBotTeam(bot->GetGUID()) == HORDE)
        {
            SendBroadcastText(BG_WS_TEXT_HORDE_FLAG_RETURNED, CHAT_MSG_BG_SYSTEM_HORDE, bot);
            UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_WAIT_RESPAWN);
            RespawnFlag(HORDE, false);
            SpawnBGObject(BG_WS_OBJECT_H_FLAG, RESPAWN_IMMEDIATELY);
            PlaySoundToAll(BG_WS_SOUND_FLAG_RETURNED);
            UpdateBotScore(bot, SCORE_FLAG_RETURNS, 1);
            _bothFlagsKept = false;
            HandleFlagRoomCapturePoint(TEAM_ALLIANCE); // Check Alliance flag if it is in capture zone; if so, capture it
        }
        else
        {
            SendBroadcastText(BG_WS_TEXT_HORDE_FLAG_PICKED_UP, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
            PlaySoundToAll(BG_WS_SOUND_HORDE_FLAG_PICKED_UP);
            SpawnBGObject(BG_WS_OBJECT_H_FLAG, RESPAWN_ONE_DAY);
            SetHordeFlagPicker(bot->GetGUID());
            bot->CastSpell(bot, BG_WS_SPELL_WARSONG_FLAG, true);
            _flagState[TEAM_HORDE] = BG_WS_FLAG_STATE_ON_PLAYER;
            UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_ON_PLAYER);
            if (_flagDebuffState == 1)
              bot->CastSpell(bot, WS_SPELL_FOCUSED_ASSAULT, true);
            else if (_flagDebuffState == 2)
              bot->CastSpell(bot, WS_SPELL_BRUTAL_ASSAULT, true);
            UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 1);
        }
        //called in HandleGameObjectUseOpcode:
        target_obj->Delete();
    }

    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
}

void BattlegroundWS::RemoveBot(ObjectGuid guid)
{
    // sometimes flag aura not removed :(
    if (IsAllianceFlagPickedup() && m_FlagKeepers[TEAM_ALLIANCE] == guid)
    {
        Creature const* bot = BotDataMgr::FindBot(guid.GetEntry());
        if (!bot)
        {
            TC_LOG_ERROR("bg.battleground", "BattlegroundWS: Removing offline bot {} who has the FLAG!!", guid.GetEntry());
            SetAllianceFlagPicker(ObjectGuid::Empty);
            RespawnFlag(ALLIANCE, false);
        }
        else
            EventBotDroppedFlag(const_cast<Creature*>(bot));
    }
    if (IsHordeFlagPickedup() && m_FlagKeepers[TEAM_HORDE] == guid)
    {
        Creature const* bot = BotDataMgr::FindBot(guid.GetEntry());
        if (!bot)
        {
            TC_LOG_ERROR("bg.battleground", "BattlegroundWS: Removing offline bot {} who has the FLAG!!", guid.GetEntry());
            SetHordeFlagPicker(ObjectGuid::Empty);
            RespawnFlag(HORDE, false);
        }
        else
            EventBotDroppedFlag(const_cast<Creature*>(bot));
    }
}
//end npcbot

void BattlegroundWS::RemovePlayer(Player* player, ObjectGuid guid, uint32 /*team*/)
{
    // sometimes flag aura not removed :(
    if (IsAllianceFlagPickedup() && m_FlagKeepers[TEAM_ALLIANCE] == guid)
    {
        if (!player)
        {
            TC_LOG_ERROR("bg.battleground", "BattlegroundWS: Removing offline player who has the FLAG!!");
            SetAllianceFlagPicker(ObjectGuid::Empty);
            RespawnFlag(ALLIANCE, false);
        }
        else
            EventPlayerDroppedFlag(player);
    }
    if (IsHordeFlagPickedup() && m_FlagKeepers[TEAM_HORDE] == guid)
    {
        if (!player)
        {
            TC_LOG_ERROR("bg.battleground", "BattlegroundWS: Removing offline player who has the FLAG!!");
            SetHordeFlagPicker(ObjectGuid::Empty);
            RespawnFlag(HORDE, false);
        }
        else
            EventPlayerDroppedFlag(player);
    }
}

void BattlegroundWS::UpdateFlagState(uint32 team, uint32 value)
{
    if (team == ALLIANCE)
        UpdateWorldState(BG_WS_FLAG_STATE_ALLIANCE, value);
    else
        UpdateWorldState(BG_WS_FLAG_STATE_HORDE, value);
}

void BattlegroundWS::UpdateTeamScore(uint32 team)
{
    if (team == TEAM_ALLIANCE)
        UpdateWorldState(BG_WS_FLAG_CAPTURES_ALLIANCE, GetTeamScore(team));
    else
        UpdateWorldState(BG_WS_FLAG_CAPTURES_HORDE, GetTeamScore(team));
}

void BattlegroundWS::HandleAreaTrigger(Player* player, uint32 trigger)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    //uint32 SpellId = 0;
    //uint64 buff_guid = 0;
    switch (trigger)
    {
        case 3686:                                          // Alliance elixir of speed spawn. Trigger not working, because located inside other areatrigger, can be replaced by IsWithinDist(object, dist) in Battleground::Update().
            //buff_guid = BgObjects[BG_WS_OBJECT_SPEEDBUFF_1];
            break;
        case 3687:                                          // Horde elixir of speed spawn. Trigger not working, because located inside other areatrigger, can be replaced by IsWithinDist(object, dist) in Battleground::Update().
            //buff_guid = BgObjects[BG_WS_OBJECT_SPEEDBUFF_2];
            break;
        case 3706:                                          // Alliance elixir of regeneration spawn
            //buff_guid = BgObjects[BG_WS_OBJECT_REGENBUFF_1];
            break;
        case 3708:                                          // Horde elixir of regeneration spawn
            //buff_guid = BgObjects[BG_WS_OBJECT_REGENBUFF_2];
            break;
        case 3707:                                          // Alliance elixir of berserk spawn
            //buff_guid = BgObjects[BG_WS_OBJECT_BERSERKBUFF_1];
            break;
        case 3709:                                          // Horde elixir of berserk spawn
            //buff_guid = BgObjects[BG_WS_OBJECT_BERSERKBUFF_2];
            break;
        case 3646:                                          // Alliance Flag spawn
            if (_flagState[TEAM_HORDE] && !_flagState[TEAM_ALLIANCE])
                if (GetFlagPickerGUID(TEAM_HORDE) == player->GetGUID())
                    EventPlayerCapturedFlag(player);
            break;
        case 3647:                                          // Horde Flag spawn
            if (_flagState[TEAM_ALLIANCE] && !_flagState[TEAM_HORDE])
                if (GetFlagPickerGUID(TEAM_ALLIANCE) == player->GetGUID())
                    EventPlayerCapturedFlag(player);
            break;
        case 3649:                                          // unk1
        case 3688:                                          // unk2
        case 4628:                                          // unk3
        case 4629:                                          // unk4
            break;
        default:
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }

    //if (buff_guid)
    //    HandleTriggerBuff(buff_guid, player);
}

//npcbot
void BattlegroundWS::HandleBotAreaTrigger(Creature* bot, uint32 trigger)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 3686:                                          // Alliance elixir of speed spawn. Trigger not working, because located inside other areatrigger, can be replaced by IsWithinDist(object, dist) in Battleground::Update().
        case 3687:                                          // Horde elixir of speed spawn. Trigger not working, because located inside other areatrigger, can be replaced by IsWithinDist(object, dist) in Battleground::Update().
        case 3706:                                          // Alliance elixir of regeneration spawn
        case 3708:                                          // Horde elixir of regeneration spawn
        case 3707:                                          // Alliance elixir of berserk spawn
        case 3709:                                          // Horde elixir of berserk spawn
        case 3649:                                          // unk1
        case 3688:                                          // unk2
        case 4628:                                          // unk3
        case 4629:                                          // unk4
            break;
        case 3646:                                          // Alliance Flag spawn
            if (_flagState[TEAM_HORDE] && !_flagState[TEAM_ALLIANCE])
                if (GetFlagPickerGUID(TEAM_HORDE) == bot->GetGUID())
                    EventBotCapturedFlag(bot);
            break;
        case 3647:                                          // Horde Flag spawn
            if (_flagState[TEAM_ALLIANCE] && !_flagState[TEAM_HORDE])
                if (GetFlagPickerGUID(TEAM_ALLIANCE) == bot->GetGUID())
                    EventBotCapturedFlag(bot);
            break;
        default:
            Battleground::HandleBotAreaTrigger(bot, trigger);
            break;
    }
}
//end npcbot

bool BattlegroundWS::SetupBattleground()
{
    // flags
    if (!AddObject(BG_WS_OBJECT_A_FLAG, BG_OBJECT_A_FLAG_WS_ENTRY, 1540.423f, 1481.325f, 351.8284f, 3.089233f, 0, 0, 0.9996573f, 0.02617699f, BG_WS_FLAG_RESPAWN_TIME/1000)
        || !AddObject(BG_WS_OBJECT_H_FLAG, BG_OBJECT_H_FLAG_WS_ENTRY, 916.0226f, 1434.405f, 345.413f, 0.01745329f, 0, 0, 0.008726535f, 0.9999619f, BG_WS_FLAG_RESPAWN_TIME/1000)
        // buffs
        || !AddObject(BG_WS_OBJECT_SPEEDBUFF_1, BG_OBJECTID_SPEEDBUFF_ENTRY, 1449.93f, 1470.71f, 342.6346f, -1.64061f, 0, 0, 0.7313537f, -0.6819983f, BUFF_RESPAWN_TIME)
        || !AddObject(BG_WS_OBJECT_SPEEDBUFF_2, BG_OBJECTID_SPEEDBUFF_ENTRY, 1005.171f, 1447.946f, 335.9032f, 1.64061f, 0, 0, 0.7313537f, 0.6819984f, BUFF_RESPAWN_TIME)
        || !AddObject(BG_WS_OBJECT_REGENBUFF_1, BG_OBJECTID_REGENBUFF_ENTRY, 1317.506f, 1550.851f, 313.2344f, -0.2617996f, 0, 0, 0.1305263f, -0.9914448f, BUFF_RESPAWN_TIME)
        || !AddObject(BG_WS_OBJECT_REGENBUFF_2, BG_OBJECTID_REGENBUFF_ENTRY, 1110.451f, 1353.656f, 316.5181f, -0.6806787f, 0, 0, 0.333807f, -0.9426414f, BUFF_RESPAWN_TIME)
        || !AddObject(BG_WS_OBJECT_BERSERKBUFF_1, BG_OBJECTID_BERSERKERBUFF_ENTRY, 1320.09f, 1378.79f, 314.7532f, 1.186824f, 0, 0, 0.5591929f, 0.8290376f, BUFF_RESPAWN_TIME)
        || !AddObject(BG_WS_OBJECT_BERSERKBUFF_2, BG_OBJECTID_BERSERKERBUFF_ENTRY, 1139.688f, 1560.288f, 306.8432f, -2.443461f, 0, 0, 0.9396926f, -0.3420201f, BUFF_RESPAWN_TIME)
        // alliance gates
        || !AddObject(BG_WS_OBJECT_DOOR_A_1, BG_OBJECT_DOOR_A_1_WS_ENTRY, 1503.335f, 1493.466f, 352.1888f, 3.115414f, 0, 0, 0.9999143f, 0.01308903f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_WS_OBJECT_DOOR_A_2, BG_OBJECT_DOOR_A_2_WS_ENTRY, 1492.478f, 1457.912f, 342.9689f, 3.115414f, 0, 0, 0.9999143f, 0.01308903f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_WS_OBJECT_DOOR_A_3, BG_OBJECT_DOOR_A_3_WS_ENTRY, 1468.503f, 1494.357f, 351.8618f, 3.115414f, 0, 0, 0.9999143f, 0.01308903f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_WS_OBJECT_DOOR_A_4, BG_OBJECT_DOOR_A_4_WS_ENTRY, 1471.555f, 1458.778f, 362.6332f, 3.115414f, 0, 0, 0.9999143f, 0.01308903f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_WS_OBJECT_DOOR_A_5, BG_OBJECT_DOOR_A_5_WS_ENTRY, 1492.347f, 1458.34f, 342.3712f, -0.03490669f, 0, 0, 0.01745246f, -0.9998477f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_WS_OBJECT_DOOR_A_6, BG_OBJECT_DOOR_A_6_WS_ENTRY, 1503.466f, 1493.367f, 351.7352f, -0.03490669f, 0, 0, 0.01745246f, -0.9998477f, RESPAWN_IMMEDIATELY)
        // horde gates
        || !AddObject(BG_WS_OBJECT_DOOR_H_1, BG_OBJECT_DOOR_H_1_WS_ENTRY, 949.1663f, 1423.772f, 345.6241f, -0.5756807f, -0.01673368f, -0.004956111f, -0.2839723f, 0.9586737f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_WS_OBJECT_DOOR_H_2, BG_OBJECT_DOOR_H_2_WS_ENTRY, 953.0507f, 1459.842f, 340.6526f, -1.99662f, -0.1971825f, 0.1575096f, -0.8239487f, 0.5073641f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_WS_OBJECT_DOOR_H_3, BG_OBJECT_DOOR_H_3_WS_ENTRY, 949.9523f, 1422.751f, 344.9273f, 0.0f, 0, 0, 0, 1, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_WS_OBJECT_DOOR_H_4, BG_OBJECT_DOOR_H_4_WS_ENTRY, 950.7952f, 1459.583f, 342.1523f, 0.05235988f, 0, 0, 0.02617695f, 0.9996573f, RESPAWN_IMMEDIATELY)
)
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundWS: Failed to spawn some object Battleground not created!");
        return false;
    }

    WorldSafeLocsEntry const* sg = sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_MAIN_ALLIANCE);
    if (!sg || !AddSpiritGuide(WS_SPIRIT_MAIN_ALLIANCE, sg->Loc.X, sg->Loc.Y, sg->Loc.Z, 3.124139f, TEAM_ALLIANCE))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundWS: Failed to spawn Alliance spirit guide! Battleground not created!");
        return false;
    }

    sg = sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_MAIN_HORDE);
    if (!sg || !AddSpiritGuide(WS_SPIRIT_MAIN_HORDE, sg->Loc.X, sg->Loc.Y, sg->Loc.Z, 3.193953f, TEAM_HORDE))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundWS: Failed to spawn Horde spirit guide! Battleground not created!");
        return false;
    }

    TC_LOG_DEBUG("bg.battleground", "BatteGroundWS: BG objects and spirit guides spawned");

    return true;
}

void BattlegroundWS::Reset()
{
    //call parent's class reset
    Battleground::Reset();

    m_FlagKeepers[TEAM_ALLIANCE].Clear();
    m_FlagKeepers[TEAM_HORDE].Clear();
    m_DroppedFlagGUID[TEAM_ALLIANCE].Clear();
    m_DroppedFlagGUID[TEAM_HORDE].Clear();
    _flagState[TEAM_ALLIANCE]        = BG_WS_FLAG_STATE_ON_BASE;
    _flagState[TEAM_HORDE]           = BG_WS_FLAG_STATE_ON_BASE;
    m_TeamScores[TEAM_ALLIANCE]      = 0;
    m_TeamScores[TEAM_HORDE]         = 0;

    if (sBattlegroundMgr->IsBGWeekend(GetTypeID()))
    {
        m_ReputationCapture = 45;
        m_HonorWinKills = 3;
        m_HonorEndKills = 4;
    }
    else
    {
        m_ReputationCapture = 35;
        m_HonorWinKills = 1;
        m_HonorEndKills = 2;
    }
    _minutesElapsed                  = 0;
    _lastFlagCaptureTeam             = 0;
    _bothFlagsKept                   = false;
    _flagDebuffState                 = 0;
    _flagSpellForceTimer             = 0;
    _flagsDropTimer[TEAM_ALLIANCE]   = 0;
    _flagsDropTimer[TEAM_HORDE]      = 0;
    _flagsTimer[TEAM_ALLIANCE]       = 0;
    _flagsTimer[TEAM_HORDE]          = 0;
}

void BattlegroundWS::EndBattleground(uint32 winner)
{
    // Win reward
    if (winner == ALLIANCE)
        RewardHonorToTeam(GetBonusHonorFromKill(m_HonorWinKills), ALLIANCE);
    if (winner == HORDE)
        RewardHonorToTeam(GetBonusHonorFromKill(m_HonorWinKills), HORDE);
    // Complete map_end rewards (even if no team wins)
    RewardHonorToTeam(GetBonusHonorFromKill(m_HonorEndKills), ALLIANCE);
    RewardHonorToTeam(GetBonusHonorFromKill(m_HonorEndKills), HORDE);

    Battleground::EndBattleground(winner);
}

void BattlegroundWS::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    EventPlayerDroppedFlag(player);

    Battleground::HandleKillPlayer(player, killer);
}

//npcbot
void BattlegroundWS::HandleBotKillPlayer(Creature* killer, Player* victim)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    EventPlayerDroppedFlag(victim);
    Battleground::HandleBotKillPlayer(killer, victim);
}
void BattlegroundWS::HandleBotKillBot(Creature* killer, Creature* victim)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    EventBotDroppedFlag(victim);
    Battleground::HandleBotKillBot(killer, victim);
}
void BattlegroundWS::HandlePlayerKillBot(Creature* victim, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    EventBotDroppedFlag(victim);
    Battleground::HandlePlayerKillBot(victim, killer);
}
//end npcbot

bool BattlegroundWS::UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor)
{
    if (!Battleground::UpdatePlayerScore(player, type, value, doAddHonor))
        return false;

    switch (type)
    {
        case SCORE_FLAG_CAPTURES:                           // flags captured
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, WS_OBJECTIVE_CAPTURE_FLAG);
            break;
        case SCORE_FLAG_RETURNS:                            // flags returned
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, WS_OBJECTIVE_RETURN_FLAG);
            break;
        default:
            break;
    }
    return true;
}

//npcbot
bool BattlegroundWS::UpdateBotScore(Creature const* bot, uint32 type, uint32 value, bool doAddHonor)
{
    if (!Battleground::UpdateBotScore(bot, type, value, doAddHonor))
        return false;

    return true;
}
//end npcbot

WorldSafeLocsEntry const* BattlegroundWS::GetClosestGraveyard(Player* player)
{
    //if status in progress, it returns main graveyards with spiritguides
    //else it will return the graveyard in the flagroom - this is especially good
    //if a player dies in preparation phase - then the player can't cheat
    //and teleport to the graveyard outside the flagroom
    //and start running around, while the doors are still closed
    if (player->GetTeam() == ALLIANCE)
    {
        if (GetStatus() == STATUS_IN_PROGRESS)
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_MAIN_ALLIANCE);
        else
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_FLAGROOM_ALLIANCE);
    }
    else
    {
        if (GetStatus() == STATUS_IN_PROGRESS)
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_MAIN_HORDE);
        else
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_FLAGROOM_HORDE);
    }
}

//npcbot
WorldSafeLocsEntry const* BattlegroundWS::GetClosestGraveyardForBot(WorldLocation const& /*curPos*/, uint32 team) const
{
    if (team == ALLIANCE)
    {
        if (GetStatus() == STATUS_IN_PROGRESS)
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_MAIN_ALLIANCE);
        else
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_FLAGROOM_ALLIANCE);
    }
    else
    {
        if (GetStatus() == STATUS_IN_PROGRESS)
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_MAIN_HORDE);
        else
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_FLAGROOM_HORDE);
    }
}
//end npcbot

void BattlegroundWS::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(BG_WS_FLAG_CAPTURES_ALLIANCE, GetTeamScore(TEAM_ALLIANCE));
    packet.Worldstates.emplace_back(BG_WS_FLAG_CAPTURES_HORDE, GetTeamScore(TEAM_HORDE));

    if (_flagState[TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_GROUND)
        packet.Worldstates.emplace_back(BG_WS_FLAG_UNK_ALLIANCE, uint32(-1)); // ??
    else if (_flagState[TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_PLAYER)
        packet.Worldstates.emplace_back(BG_WS_FLAG_UNK_ALLIANCE, 1);
    else
        packet.Worldstates.emplace_back(BG_WS_FLAG_UNK_ALLIANCE, 0);

    if (_flagState[TEAM_HORDE] == BG_WS_FLAG_STATE_ON_GROUND)
        packet.Worldstates.emplace_back(BG_WS_FLAG_UNK_HORDE, uint32(-1)); // ??
    else if (_flagState[TEAM_HORDE] == BG_WS_FLAG_STATE_ON_PLAYER)
        packet.Worldstates.emplace_back(BG_WS_FLAG_UNK_HORDE, 1);
    else
        packet.Worldstates.emplace_back(BG_WS_FLAG_UNK_HORDE, 0);

    packet.Worldstates.emplace_back(BG_WS_FLAG_CAPTURES_MAX, BG_WS_MAX_TEAM_SCORE);

    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        packet.Worldstates.emplace_back(BG_WS_STATE_TIMER_ACTIVE, 1);
        packet.Worldstates.emplace_back(BG_WS_STATE_TIMER, 25 - _minutesElapsed);
    }
    else
        packet.Worldstates.emplace_back(BG_WS_STATE_TIMER_ACTIVE, 0);

    if (_flagState[TEAM_HORDE] == BG_WS_FLAG_STATE_ON_PLAYER)
        packet.Worldstates.emplace_back(BG_WS_FLAG_STATE_HORDE, 2);
    else
        packet.Worldstates.emplace_back(BG_WS_FLAG_STATE_HORDE, 1);

    if (_flagState[TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_PLAYER)
        packet.Worldstates.emplace_back(BG_WS_FLAG_STATE_ALLIANCE, 2);
    else
        packet.Worldstates.emplace_back(BG_WS_FLAG_STATE_ALLIANCE, 1);
}

uint32 BattlegroundWS::GetPrematureWinner()
{
    if (GetTeamScore(TEAM_ALLIANCE) > GetTeamScore(TEAM_HORDE))
        return ALLIANCE;
    else if (GetTeamScore(TEAM_HORDE) > GetTeamScore(TEAM_ALLIANCE))
        return HORDE;

    return Battleground::GetPrematureWinner();
}

bool BattlegroundWS::CheckAchievementCriteriaMeet(uint32 criteriaId, Player const* player, Unit const* target, uint32 miscValue)
{
    switch (criteriaId)
    {
        case BG_CRITERIA_CHECK_SAVE_THE_DAY:
            if (target)
                if (Player const* playerTarget = target->ToPlayer())
                    return GetFlagState(playerTarget->GetTeam()) == BG_WS_FLAG_STATE_ON_BASE;
            return false;
    }

    return Battleground::CheckAchievementCriteriaMeet(criteriaId, player, target, miscValue);
}
