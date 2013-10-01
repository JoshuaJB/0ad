/* Copyright (C) 2013 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "scriptinterface/ScriptInterface.h"

#include "graphics/Camera.h"
#include "graphics/GameView.h"
#include "graphics/MapReader.h"
#include "gui/GUIManager.h"
#include "graphics/scripting/JSInterface_GameView.h"
#include "lib/timer.h"
#include "lib/utf8.h"
#include "lib/sysdep/sysdep.h"
#include "maths/FixedVector3D.h"
#include "network/NetClient.h"
#include "network/NetServer.h"
#include "network/NetTurnManager.h"
#include "ps/CLogger.h"
#include "ps/CConsole.h"
#include "ps/Errors.h"
#include "ps/Game.h"
#include "ps/GUID.h"
#include "ps/World.h"
#include "ps/Hotkey.h"
#include "ps/Overlay.h"
#include "ps/ProfileViewer.h"
#include "ps/Pyrogenesis.h"
#include "ps/SavedGame.h"
#include "ps/scripting/JSInterface_ConfigDB.h"
#include "ps/scripting/JSInterface_Console.h"
#include "ps/UserReport.h"
#include "ps/GameSetup/Atlas.h"
#include "ps/GameSetup/Config.h"
#include "ps/ConfigDB.h"
#include "renderer/scripting/JSInterface_Renderer.h"
#include "tools/atlas/GameInterface/GameLoop.h"
#include "lobby/XmppClient.h"
#include "lobby/sha.h"

#include "simulation2/Simulation2.h"
#include "simulation2/components/ICmpAIManager.h"
#include "simulation2/components/ICmpCommandQueue.h"
#include "simulation2/components/ICmpGuiInterface.h"
#include "simulation2/components/ICmpRangeManager.h"
#include "simulation2/components/ICmpTemplateManager.h"
#include "simulation2/components/ICmpSelectable.h"
#include "simulation2/helpers/Selection.h"

#include "js/jsapi.h"
/*
 * This file defines a set of functions that are available to GUI scripts, to allow
 * interaction with the rest of the engine.
 * Functions are exposed to scripts within the global object 'Engine', so
 * scripts should call "Engine.FunctionName(...)" etc.
 */

extern void restart_mainloop_in_atlas(); // from main.cpp

namespace {

CScriptVal GetActiveGui(void* UNUSED(cbdata))
{
	return OBJECT_TO_JSVAL(g_GUI->GetScriptObject());
}

void PushGuiPage(void* UNUSED(cbdata), std::wstring name, CScriptVal initData)
{
	g_GUI->PushPage(name, initData);
}

void SwitchGuiPage(void* UNUSED(cbdata), std::wstring name, CScriptVal initData)
{
	g_GUI->SwitchPage(name, initData);
}

void PopGuiPage(void* UNUSED(cbdata))
{
	g_GUI->PopPage();
}

CScriptVal GuiInterfaceCall(void* cbdata, std::wstring name, CScriptVal data)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	if (!g_Game)
		return JSVAL_VOID;
	CSimulation2* sim = g_Game->GetSimulation2();
	ENSURE(sim);

	CmpPtr<ICmpGuiInterface> cmpGuiInterface(*sim, SYSTEM_ENTITY);
	if (!cmpGuiInterface)
		return JSVAL_VOID;

	int player = -1;
	if (g_Game)
		player = g_Game->GetPlayerID();

	CScriptValRooted arg (sim->GetScriptInterface().GetContext(), sim->GetScriptInterface().CloneValueFromOtherContext(guiManager->GetScriptInterface(), data.get()));
	CScriptVal ret (cmpGuiInterface->ScriptCall(player, name, arg.get()));
	return guiManager->GetScriptInterface().CloneValueFromOtherContext(sim->GetScriptInterface(), ret.get());
}

void PostNetworkCommand(void* cbdata, CScriptVal cmd)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	if (!g_Game)
		return;
	CSimulation2* sim = g_Game->GetSimulation2();
	ENSURE(sim);

	CmpPtr<ICmpCommandQueue> cmpCommandQueue(*sim, SYSTEM_ENTITY);
	if (!cmpCommandQueue)
		return;

	jsval cmd2 = sim->GetScriptInterface().CloneValueFromOtherContext(guiManager->GetScriptInterface(), cmd.get());

	cmpCommandQueue->PostNetworkCommand(cmd2);
}

std::vector<entity_id_t> PickEntitiesAtPoint(void* UNUSED(cbdata), int x, int y)
{
	return EntitySelection::PickEntitiesAtPoint(*g_Game->GetSimulation2(), *g_Game->GetView()->GetCamera(), x, y, g_Game->GetPlayerID(), false);
}

std::vector<entity_id_t> PickFriendlyEntitiesInRect(void* UNUSED(cbdata), int x0, int y0, int x1, int y1, int player)
{
	return EntitySelection::PickEntitiesInRect(*g_Game->GetSimulation2(), *g_Game->GetView()->GetCamera(), x0, y0, x1, y1, player, false);
}

std::vector<entity_id_t> PickFriendlyEntitiesOnScreen(void* cbdata, int player)
{
	return PickFriendlyEntitiesInRect(cbdata, 0, 0, g_xres, g_yres, player);
}

std::vector<entity_id_t> PickSimilarFriendlyEntities(void* UNUSED(cbdata), std::string templateName, bool includeOffScreen, bool matchRank, bool allowFoundations)
{
	return EntitySelection::PickSimilarEntities(*g_Game->GetSimulation2(), *g_Game->GetView()->GetCamera(), templateName, g_Game->GetPlayerID(), includeOffScreen, matchRank, false, allowFoundations);
}

CFixedVector3D GetTerrainAtScreenPoint(void* UNUSED(cbdata), int x, int y)
{
	CVector3D pos = g_Game->GetView()->GetCamera()->GetWorldCoordinates(x, y, true);
	return CFixedVector3D(fixed::FromFloat(pos.X), fixed::FromFloat(pos.Y), fixed::FromFloat(pos.Z));
}

std::wstring SetCursor(void* UNUSED(cbdata), std::wstring name)
{
	std::wstring old = g_CursorName;
	g_CursorName = name;
	return old;
}

int GetPlayerID(void* UNUSED(cbdata))
{
	if (g_Game)
		return g_Game->GetPlayerID();
	return -1;
}

void SetPlayerID(void* UNUSED(cbdata), int id)
{
	if (g_Game)
		g_Game->SetPlayerID(id);
}

void StartNetworkGame(void* UNUSED(cbdata))
{
	ENSURE(g_NetServer);
	g_NetServer->StartGame();
}

void StartGame(void* cbdata, CScriptVal attribs, int playerID)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	ENSURE(!g_NetServer);
	ENSURE(!g_NetClient);

	ENSURE(!g_Game);
	g_Game = new CGame();

	// Convert from GUI script context to sim script context
	CSimulation2* sim = g_Game->GetSimulation2();
	CScriptValRooted gameAttribs (sim->GetScriptInterface().GetContext(),
			sim->GetScriptInterface().CloneValueFromOtherContext(guiManager->GetScriptInterface(), attribs.get()));

	g_Game->SetPlayerID(playerID);
	g_Game->StartGame(gameAttribs, "");
}

CScriptVal StartSavedGame(void* cbdata, std::wstring name)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	ENSURE(!g_NetServer);
	ENSURE(!g_NetClient);

	ENSURE(!g_Game);

	// Load the saved game data from disk
	CScriptValRooted metadata;
	std::string savedState;
	Status err = SavedGames::Load(name, guiManager->GetScriptInterface(), metadata, savedState);
	if (err < 0)
		return CScriptVal();

	g_Game = new CGame();

	// Convert from GUI script context to sim script context
	CSimulation2* sim = g_Game->GetSimulation2();
	CScriptValRooted gameMetadata (sim->GetScriptInterface().GetContext(),
		sim->GetScriptInterface().CloneValueFromOtherContext(guiManager->GetScriptInterface(), metadata.get()));

	CScriptValRooted gameInitAttributes;
	sim->GetScriptInterface().GetProperty(gameMetadata.get(), "initAttributes", gameInitAttributes);

	int playerID;
	sim->GetScriptInterface().GetProperty(gameMetadata.get(), "player", playerID);

	// Start the game
	g_Game->SetPlayerID(playerID);
	g_Game->StartGame(gameInitAttributes, savedState);

	return metadata.get();
}

void SaveGame(void* cbdata, std::wstring filename, std::wstring description)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	if (SavedGames::Save(filename, description, *g_Game->GetSimulation2(), guiManager, g_Game->GetPlayerID()) < 0)
		LOGERROR(L"Failed to save game");
}

void SaveGamePrefix(void* cbdata, std::wstring prefix, std::wstring description)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	if (SavedGames::SavePrefix(prefix, description, *g_Game->GetSimulation2(), guiManager, g_Game->GetPlayerID()) < 0)
		LOGERROR(L"Failed to save game");
}

void SetNetworkGameAttributes(void* cbdata, CScriptVal attribs)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	ENSURE(g_NetServer);

	g_NetServer->UpdateGameAttributes(attribs, guiManager->GetScriptInterface());
}

void StartNetworkHost(void* cbdata, std::wstring playerName)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	ENSURE(!g_NetClient);
	ENSURE(!g_NetServer);
	ENSURE(!g_Game);

	g_NetServer = new CNetServer();
	if (!g_NetServer->SetupConnection())
	{
		guiManager->GetScriptInterface().ReportError("Failed to start server");
		SAFE_DELETE(g_NetServer);
		return;
	}

	g_Game = new CGame();
	g_NetClient = new CNetClient(g_Game);
	g_NetClient->SetUserName(playerName);

	if (!g_NetClient->SetupConnection("127.0.0.1"))
	{
		guiManager->GetScriptInterface().ReportError("Failed to connect to server");
		SAFE_DELETE(g_NetClient);
		SAFE_DELETE(g_Game);
	}
}

void StartNetworkJoin(void* cbdata, std::wstring playerName, std::string serverAddress)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	ENSURE(!g_NetClient);
	ENSURE(!g_NetServer);
	ENSURE(!g_Game);

	g_Game = new CGame();
	g_NetClient = new CNetClient(g_Game);
	g_NetClient->SetUserName(playerName);
	if (!g_NetClient->SetupConnection(serverAddress))
	{
		guiManager->GetScriptInterface().ReportError("Failed to connect to server");
		SAFE_DELETE(g_NetClient);
		SAFE_DELETE(g_Game);
	}
}

void DisconnectNetworkGame(void* UNUSED(cbdata))
{
	// TODO: we ought to do async reliable disconnections

	SAFE_DELETE(g_NetServer);
	SAFE_DELETE(g_NetClient);
	SAFE_DELETE(g_Game);
}

CScriptVal PollNetworkClient(void* cbdata)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	if (!g_NetClient)
		return CScriptVal();

	CScriptValRooted poll = g_NetClient->GuiPoll();

	// Convert from net client context to GUI script context
	return guiManager->GetScriptInterface().CloneValueFromOtherContext(g_NetClient->GetScriptInterface(), poll.get());
}

void AssignNetworkPlayer(void* UNUSED(cbdata), int playerID, std::string guid)
{
	ENSURE(g_NetServer);

	g_NetServer->AssignPlayer(playerID, guid);
}

void SendNetworkChat(void* UNUSED(cbdata), std::wstring message)
{
	ENSURE(g_NetClient);

	g_NetClient->SendChatMessage(message);
}

std::vector<CScriptValRooted> GetAIs(void* cbdata)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	return ICmpAIManager::GetAIs(guiManager->GetScriptInterface());
}

std::vector<CScriptValRooted> GetSavedGames(void* cbdata)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	return SavedGames::GetSavedGames(guiManager->GetScriptInterface());
}

bool DeleteSavedGame(void* UNUSED(cbdata), std::wstring name)
{
	return SavedGames::DeleteSavedGame(name);
}

void OpenURL(void* UNUSED(cbdata), std::string url)
{
	sys_open_url(url);
}

std::wstring GetMatchID(void* UNUSED(cbdata))
{
	return ps_generate_guid().FromUTF8();
}

void RestartInAtlas(void* UNUSED(cbdata))
{
	restart_mainloop_in_atlas();
}

bool AtlasIsAvailable(void* UNUSED(cbdata))
{
	return ATLAS_IsAvailable();
}

bool IsAtlasRunning(void* UNUSED(cbdata))
{
	return (g_AtlasGameLoop && g_AtlasGameLoop->running);
}

CScriptVal LoadMapSettings(void* cbdata, VfsPath pathname)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	CMapSummaryReader reader;

	if (reader.LoadMap(pathname.ChangeExtension(L".xml")) != PSRETURN_OK)
		return CScriptVal();

	return reader.GetMapSettings(guiManager->GetScriptInterface()).get();
}

CScriptVal GetMapSettings(void* cbdata)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	if (!g_Game)
		return CScriptVal();

	return guiManager->GetScriptInterface().CloneValueFromOtherContext(
		g_Game->GetSimulation2()->GetScriptInterface(),
		g_Game->GetSimulation2()->GetMapSettings().get());
}

/**
 * Get the current X coordinate of the camera.
 */
float CameraGetX(void* UNUSED(cbdata))
{
	if (g_Game && g_Game->GetView())
		return g_Game->GetView()->GetCameraX();
	return -1;
}

/**
 * Get the current Z coordinate of the camera.
 */
float CameraGetZ(void* UNUSED(cbdata))
{
	if (g_Game && g_Game->GetView())
		return g_Game->GetView()->GetCameraZ();
	return -1;
}

/**
 * Start / stop camera following mode
 * @param entityid unit id to follow. If zero, stop following mode
 */
void CameraFollow(void* UNUSED(cbdata), entity_id_t entityid)
{
	if (g_Game && g_Game->GetView())
		g_Game->GetView()->CameraFollow(entityid, false);
}

/**
 * Start / stop first-person camera following mode
 * @param entityid unit id to follow. If zero, stop following mode
 */
void CameraFollowFPS(void* UNUSED(cbdata), entity_id_t entityid)
{
	if (g_Game && g_Game->GetView())
		g_Game->GetView()->CameraFollow(entityid, true);
}

/// Move camera to a 2D location
void CameraMoveTo(void* UNUSED(cbdata), entity_pos_t x, entity_pos_t z)
{
	// called from JS; must not fail
	if(!(g_Game && g_Game->GetWorld() && g_Game->GetView() && g_Game->GetWorld()->GetTerrain()))
		return;

	CTerrain* terrain = g_Game->GetWorld()->GetTerrain();

	CVector3D target;
	target.X = x.ToFloat();
	target.Z = z.ToFloat();
	target.Y = terrain->GetExactGroundLevel(target.X, target.Z);

	g_Game->GetView()->MoveCameraTarget(target);
}

entity_id_t GetFollowedEntity(void* UNUSED(cbdata))
{
	if (g_Game && g_Game->GetView())
		return g_Game->GetView()->GetFollowedEntity();

	return INVALID_ENTITY;
}

bool HotkeyIsPressed_(void* UNUSED(cbdata), std::string hotkeyName)
{
	return HotkeyIsPressed(hotkeyName);
}

void DisplayErrorDialog(void* UNUSED(cbdata), std::wstring msg)
{
	debug_DisplayError(msg.c_str(), DE_NO_DEBUG_INFO, NULL, NULL, NULL, 0, NULL, NULL);
}

CScriptVal GetProfilerState(void* cbdata)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	return g_ProfileViewer.SaveToJS(guiManager->GetScriptInterface());
}


bool IsUserReportEnabled(void* UNUSED(cbdata))
{
	return g_UserReporter.IsReportingEnabled();
}

bool IsSplashScreenEnabled(void* UNUSED(cbdata))
{
	bool splashScreenEnable = true;
	CFG_GET_VAL("splashscreenenable", Bool, splashScreenEnable);
	return splashScreenEnable;
}

void SetSplashScreenEnabled(void* UNUSED(cbdata), bool enabled)
{
	CStr val = (enabled ? "true" : "false");
	g_ConfigDB.CreateValue(CFG_USER, "splashscreenenable")->m_String = val;
	g_ConfigDB.WriteFile(CFG_USER);
}


void SetUserReportEnabled(void* UNUSED(cbdata), bool enabled)
{
	g_UserReporter.SetReportingEnabled(enabled);
}

std::string GetUserReportStatus(void* UNUSED(cbdata))
{
	return g_UserReporter.GetStatus();
}

void SubmitUserReport(void* UNUSED(cbdata), std::string type, int version, std::wstring data)
{
	g_UserReporter.SubmitReport(type.c_str(), version, utf8_from_wstring(data));
}



void SetSimRate(void* UNUSED(cbdata), float rate)
{
	g_Game->SetSimRate(rate);
}

float GetSimRate(void* UNUSED(cbdata))
{
	return g_Game->GetSimRate();
}

void SetTurnLength(void* UNUSED(cbdata), int length)
{
	if (g_NetServer)
		g_NetServer->SetTurnLength(length);
	else
		LOGERROR(L"Only network host can change turn length");
}

// Focus the game camera on a given position.
void SetCameraTarget(void* UNUSED(cbdata), float x, float y, float z)
{
	g_Game->GetView()->ResetCameraTarget(CVector3D(x, y, z));
}

// Deliberately cause the game to crash.
// Currently implemented via access violation (read of address 0).
// Useful for testing the crashlog/stack trace code.
int Crash(void* UNUSED(cbdata))
{
	debug_printf(L"Crashing at user's request.\n");
	return *(volatile int*)0;
}

void DebugWarn(void* UNUSED(cbdata))
{
	debug_warn(L"Warning at user's request.");
}

// Force a JS garbage collection cycle to take place immediately.
// Writes an indication of how long this took to the console.
void ForceGC(void* cbdata)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	double time = timer_Time();
	JS_GC(guiManager->GetScriptInterface().GetContext());
	time = timer_Time() - time;
	g_Console->InsertMessage(L"Garbage collection completed in: %f", time);
}

void DumpSimState(void* UNUSED(cbdata))
{
	OsPath path = psLogDir()/"sim_dump.txt";
	std::ofstream file (OsString(path).c_str(), std::ofstream::out | std::ofstream::trunc);
	g_Game->GetSimulation2()->DumpDebugState(file);
}

void DumpTerrainMipmap(void* UNUSED(cbdata))
{
	VfsPath filename(L"screenshots/terrainmipmap.png");
	g_Game->GetWorld()->GetTerrain()->GetHeightMipmap().DumpToDisk(filename);
	OsPath realPath;
	g_VFS->GetRealPath(filename, realPath);
	LOGMESSAGERENDER(L"Terrain mipmap written to '%ls'", realPath.string().c_str());
}

void EnableTimeWarpRecording(void* UNUSED(cbdata), unsigned int numTurns)
{
	g_Game->GetTurnManager()->EnableTimeWarpRecording(numTurns);
}

void RewindTimeWarp(void* UNUSED(cbdata))
{
	g_Game->GetTurnManager()->RewindTimeWarp();
}

/* Begin lobby related functions */

void StartXmppClient(void* cbdata, std::string sUsername, std::string sPassword, std::string sRoom, std::string sNick)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	ENSURE(!g_XmppClient);

	g_XmppClient = new XmppClient(guiManager->GetScriptInterface(), sUsername, sPassword, sRoom, sNick);
	g_rankedGame = true;
}

void StartRegisterXmppClient(void* cbdata, std::string sUsername, std::string sPassword)
{
	CGUIManager* guiManager = static_cast<CGUIManager*> (cbdata);

	ENSURE(!g_XmppClient);

	g_XmppClient = new XmppClient(guiManager->GetScriptInterface(), sUsername, sPassword, "", "", true);
}

bool HasXmppClient(void* UNUSED(cbdata))
{
	return (g_XmppClient ? true : false);
}

void StopXmppClient(void* UNUSED(cbdata))
{
	ENSURE(g_XmppClient);
	SAFE_DELETE(g_XmppClient);
	g_rankedGame = false;
}

void ConnectXmppClient(void* UNUSED(cbdata))
{
	ENSURE(g_XmppClient);
	g_XmppClient->connect();
}

void DisconnectXmppClient(void* UNUSED(cbdata))
{
	ENSURE(g_XmppClient);
	g_XmppClient->disconnect();
}

void RecvXmppClient(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return;
	g_XmppClient->recv();
}

void SendGetGameList(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return;
	g_XmppClient->SendIqGetGameList();
}

void SendGetBoardList(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return;
	g_XmppClient->SendIqGetBoardList();
}

void SendGameReport(void* UNUSED(cbdata), CScriptVal data)
{
	if (!g_XmppClient)
		return;
	g_XmppClient->SendIqGameReport(data);
}

void SendRegisterGame(void* UNUSED(cbdata), CScriptVal data)
{
	if (!g_XmppClient)
		return;
	g_XmppClient->SendIqRegisterGame(data);
}

void SendUnregisterGame(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return;
	g_XmppClient->SendIqUnregisterGame();
}

void SendChangeStateGame(void* UNUSED(cbdata), std::string nbp, std::string players)
{
	if (!g_XmppClient)
		return;
	g_XmppClient->SendIqChangeStateGame(nbp, players);
}

CScriptVal GetPlayerList(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return CScriptVal();

	CScriptValRooted playerList = g_XmppClient->GUIGetPlayerList();

	return playerList.get();
}

CScriptVal GetGameList(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return CScriptVal();

	CScriptValRooted gameList = g_XmppClient->GUIGetGameList();

	return gameList.get();
}

CScriptVal GetBoardList(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return CScriptVal();

	CScriptValRooted boardList = g_XmppClient->GUIGetBoardList();

	return boardList.get();
}

CScriptVal LobbyGuiPollMessage(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return CScriptVal();

	CScriptValRooted poll = g_XmppClient->GuiPollMessage();

	return poll.get();
}

void LobbySendMessage(void* UNUSED(cbdata), std::string message)
{
	if (!g_XmppClient)
		return;

	g_XmppClient->SendMUCMessage(message);
}

void LobbySetPlayerPresence(void* UNUSED(cbdata), std::string presence)
{
	if (!g_XmppClient)
		return;

	g_XmppClient->SetPresence(presence);
}

void LobbySetNick(void* UNUSED(cbdata), std::string nick)
{
	if (!g_XmppClient)
		return;

	g_XmppClient->SetNick(nick);
}

std::string LobbyGetNick(void* UNUSED(cbdata))
{
	if (!g_XmppClient)
		return "";

	std::string nick;
	g_XmppClient->GetNick(nick);
	return nick;
}

void LobbyKick(void* UNUSED(cbdata), std::string nick, std::string reason)
{
	if (!g_XmppClient)
		return;

	g_XmppClient->kick(nick, reason);
}

void LobbyBan(void* UNUSED(cbdata), std::string nick, std::string reason)
{
	if (!g_XmppClient)
		return;

	g_XmppClient->ban(nick, reason);
}

std::string LobbyGetPlayerPresence(void* UNUSED(cbdata), std::string nickname)
{
	if (!g_XmppClient)
		return "";

	std::string presence;
	g_XmppClient->GetPresence(nickname, presence);
	return presence;
}

/* End lobby related functions */

void QuickSave(void* UNUSED(cbdata))
{
	g_Game->GetTurnManager()->QuickSave();
}

void QuickLoad(void* UNUSED(cbdata))
{
	g_Game->GetTurnManager()->QuickLoad();
}

void SetBoundingBoxDebugOverlay(void* UNUSED(cbdata), bool enabled)
{
	ICmpSelectable::ms_EnableDebugOverlays = enabled;
}

// Non-public secure PBKDF2 hash function with salting and 1,337 iterations
void EncryptPassword(const std::string& username, std::string& password)
{
        const int DIGESTSIZE = SHA_DIGEST_SIZE;
        const int ITERATIONS = 1337;

        static const byte salt_base[DIGESTSIZE] = {
                244, 243, 249, 244, 32, 33, 34, 35, 10, 11, 12, 13, 14, 15, 16, 17,
                18, 19, 20, 32, 33, 244, 224, 127, 129, 130, 140, 153, 133, 123, 234, 123 };

        // initialize the salt buffer
        byte salt_buffer[DIGESTSIZE] = {0};
        SHA256 hash;
        hash.update(salt_base, username.length());
        hash.update(username.c_str(), username.length());
        hash.finish(salt_buffer);

        // PBKDF2 to create the buffer
        byte encrypted[DIGESTSIZE];
        pbkdf2(encrypted, (byte*)password.c_str(), password.length(), salt_buffer, DIGESTSIZE, ITERATIONS);

        static const char base16[] = "0123456789ABCDEF";
        char hex[2 * DIGESTSIZE];
        for(int i = 0; i < DIGESTSIZE; ++i)
        {
                hex[i*2] = base16[encrypted[i] >> 4];           // 4 high bits
                hex[i*2 + 1] = base16[encrypted[i] & 0x0F];// 4 low bits
        }
        password.assign(hex, sizeof(hex));
}

// Public hash interface.
std::string EncryptPassword(void* UNUSED(cbdata), std::string user, std::string pass)
{
        EncryptPassword(user, pass);
        return pass;
}

bool IsRankedGame(void* UNUSED(cbdata))
{
	return g_rankedGame;
}

void SetRankedGame(void* UNUSED(cbdata), bool isRanked)
{
	g_rankedGame = isRanked;
}

} // namespace

void GuiScriptingInit(ScriptInterface& scriptInterface)
{
	JSI_GameView::RegisterScriptFunctions(scriptInterface);
	JSI_Renderer::RegisterScriptFunctions(scriptInterface);
	JSI_Console::RegisterScriptFunctions(scriptInterface);
	JSI_ConfigDB::RegisterScriptFunctions(scriptInterface);

	// GUI manager functions:
	scriptInterface.RegisterFunction<CScriptVal, &GetActiveGui>("GetActiveGui");
	scriptInterface.RegisterFunction<void, std::wstring, CScriptVal, &PushGuiPage>("PushGuiPage");
	scriptInterface.RegisterFunction<void, std::wstring, CScriptVal, &SwitchGuiPage>("SwitchGuiPage");
	scriptInterface.RegisterFunction<void, &PopGuiPage>("PopGuiPage");

	// Simulation<->GUI interface functions:
	scriptInterface.RegisterFunction<CScriptVal, std::wstring, CScriptVal, &GuiInterfaceCall>("GuiInterfaceCall");
	scriptInterface.RegisterFunction<void, CScriptVal, &PostNetworkCommand>("PostNetworkCommand");

	// Entity picking
	scriptInterface.RegisterFunction<std::vector<entity_id_t>, int, int, &PickEntitiesAtPoint>("PickEntitiesAtPoint");
	scriptInterface.RegisterFunction<std::vector<entity_id_t>, int, int, int, int, int, &PickFriendlyEntitiesInRect>("PickFriendlyEntitiesInRect");
	scriptInterface.RegisterFunction<std::vector<entity_id_t>, int, &PickFriendlyEntitiesOnScreen>("PickFriendlyEntitiesOnScreen");
	scriptInterface.RegisterFunction<std::vector<entity_id_t>, std::string, bool, bool, bool, &PickSimilarFriendlyEntities>("PickSimilarFriendlyEntities");
	scriptInterface.RegisterFunction<CFixedVector3D, int, int, &GetTerrainAtScreenPoint>("GetTerrainAtScreenPoint");

	// Network / game setup functions
	scriptInterface.RegisterFunction<void, &StartNetworkGame>("StartNetworkGame");
	scriptInterface.RegisterFunction<void, CScriptVal, int, &StartGame>("StartGame");
	scriptInterface.RegisterFunction<void, std::wstring, &StartNetworkHost>("StartNetworkHost");
	scriptInterface.RegisterFunction<void, std::wstring, std::string, &StartNetworkJoin>("StartNetworkJoin");
	scriptInterface.RegisterFunction<void, &DisconnectNetworkGame>("DisconnectNetworkGame");
	scriptInterface.RegisterFunction<CScriptVal, &PollNetworkClient>("PollNetworkClient");
	scriptInterface.RegisterFunction<void, CScriptVal, &SetNetworkGameAttributes>("SetNetworkGameAttributes");
	scriptInterface.RegisterFunction<void, int, std::string, &AssignNetworkPlayer>("AssignNetworkPlayer");
	scriptInterface.RegisterFunction<void, std::wstring, &SendNetworkChat>("SendNetworkChat");
	scriptInterface.RegisterFunction<std::vector<CScriptValRooted>, &GetAIs>("GetAIs");

	// Saved games
	scriptInterface.RegisterFunction<CScriptVal, std::wstring, &StartSavedGame>("StartSavedGame");
	scriptInterface.RegisterFunction<std::vector<CScriptValRooted>, &GetSavedGames>("GetSavedGames");
	scriptInterface.RegisterFunction<bool, std::wstring, &DeleteSavedGame>("DeleteSavedGame");
	scriptInterface.RegisterFunction<void, std::wstring, std::wstring, &SaveGame>("SaveGame");
	scriptInterface.RegisterFunction<void, std::wstring, std::wstring, &SaveGamePrefix>("SaveGamePrefix");
	scriptInterface.RegisterFunction<void, &QuickSave>("QuickSave");
	scriptInterface.RegisterFunction<void, &QuickLoad>("QuickLoad");

	// Misc functions
	scriptInterface.RegisterFunction<std::wstring, std::wstring, &SetCursor>("SetCursor");
	scriptInterface.RegisterFunction<int, &GetPlayerID>("GetPlayerID");
	scriptInterface.RegisterFunction<void, int, &SetPlayerID>("SetPlayerID");
	scriptInterface.RegisterFunction<void, std::string, &OpenURL>("OpenURL");
	scriptInterface.RegisterFunction<std::wstring, &GetMatchID>("GetMatchID");
	scriptInterface.RegisterFunction<void, &RestartInAtlas>("RestartInAtlas");
	scriptInterface.RegisterFunction<bool, &AtlasIsAvailable>("AtlasIsAvailable");
	scriptInterface.RegisterFunction<bool, &IsAtlasRunning>("IsAtlasRunning");
	scriptInterface.RegisterFunction<CScriptVal, VfsPath, &LoadMapSettings>("LoadMapSettings");
	scriptInterface.RegisterFunction<CScriptVal, &GetMapSettings>("GetMapSettings");
	scriptInterface.RegisterFunction<float, &CameraGetX>("CameraGetX");
	scriptInterface.RegisterFunction<float, &CameraGetZ>("CameraGetZ");
	scriptInterface.RegisterFunction<void, entity_id_t, &CameraFollow>("CameraFollow");
	scriptInterface.RegisterFunction<void, entity_id_t, &CameraFollowFPS>("CameraFollowFPS");
	scriptInterface.RegisterFunction<void, entity_pos_t, entity_pos_t, &CameraMoveTo>("CameraMoveTo");
	scriptInterface.RegisterFunction<entity_id_t, &GetFollowedEntity>("GetFollowedEntity");
	scriptInterface.RegisterFunction<bool, std::string, &HotkeyIsPressed_>("HotkeyIsPressed");
	scriptInterface.RegisterFunction<void, std::wstring, &DisplayErrorDialog>("DisplayErrorDialog");
	scriptInterface.RegisterFunction<CScriptVal, &GetProfilerState>("GetProfilerState");

	// User report functions
	scriptInterface.RegisterFunction<bool, &IsUserReportEnabled>("IsUserReportEnabled");
	scriptInterface.RegisterFunction<void, bool, &SetUserReportEnabled>("SetUserReportEnabled");
	scriptInterface.RegisterFunction<std::string, &GetUserReportStatus>("GetUserReportStatus");
	scriptInterface.RegisterFunction<void, std::string, int, std::wstring, &SubmitUserReport>("SubmitUserReport");

	// Splash screen functions
	scriptInterface.RegisterFunction<bool, &IsSplashScreenEnabled>("IsSplashScreenEnabled");
	scriptInterface.RegisterFunction<void, bool, &SetSplashScreenEnabled>("SetSplashScreenEnabled");

	// Development/debugging functions
	scriptInterface.RegisterFunction<void, float, &SetSimRate>("SetSimRate");
	scriptInterface.RegisterFunction<float, &GetSimRate>("GetSimRate");
	scriptInterface.RegisterFunction<void, int, &SetTurnLength>("SetTurnLength");
	scriptInterface.RegisterFunction<void, float, float, float, &SetCameraTarget>("SetCameraTarget");
	scriptInterface.RegisterFunction<int, &Crash>("Crash");
	scriptInterface.RegisterFunction<void, &DebugWarn>("DebugWarn");
	scriptInterface.RegisterFunction<void, &ForceGC>("ForceGC");
	scriptInterface.RegisterFunction<void, &DumpSimState>("DumpSimState");
	scriptInterface.RegisterFunction<void, &DumpTerrainMipmap>("DumpTerrainMipmap");
	scriptInterface.RegisterFunction<void, unsigned int, &EnableTimeWarpRecording>("EnableTimeWarpRecording");
	scriptInterface.RegisterFunction<void, &RewindTimeWarp>("RewindTimeWarp");
	scriptInterface.RegisterFunction<void, bool, &SetBoundingBoxDebugOverlay>("SetBoundingBoxDebugOverlay");

#if CONFIG2_LOBBY // Allow the lobby to be disabled
	// Lobby functions
	scriptInterface.RegisterFunction<void, std::string, std::string, std::string, std::string, &StartXmppClient>("StartXmppClient");
	scriptInterface.RegisterFunction<void, std::string, std::string, &StartRegisterXmppClient>("StartRegisterXmppClient");
	scriptInterface.RegisterFunction<bool, &HasXmppClient>("HasXmppClient");
	scriptInterface.RegisterFunction<void, &StopXmppClient>("StopXmppClient");
	scriptInterface.RegisterFunction<void, &ConnectXmppClient>("ConnectXmppClient");
	scriptInterface.RegisterFunction<void, &DisconnectXmppClient>("DisconnectXmppClient");
	scriptInterface.RegisterFunction<void, &RecvXmppClient>("RecvXmppClient");
	scriptInterface.RegisterFunction<void, &SendGetGameList>("SendGetGameList");
	scriptInterface.RegisterFunction<void, &SendGetBoardList>("SendGetBoardList");
	scriptInterface.RegisterFunction<void, CScriptVal, &SendRegisterGame>("SendRegisterGame");
	scriptInterface.RegisterFunction<void, CScriptVal, &SendGameReport>("SendGameReport");
	scriptInterface.RegisterFunction<void, &SendUnregisterGame>("SendUnregisterGame");
	scriptInterface.RegisterFunction<void, std::string, std::string, &SendChangeStateGame>("SendChangeStateGame");
	scriptInterface.RegisterFunction<CScriptVal, &GetPlayerList>("GetPlayerList");
	scriptInterface.RegisterFunction<CScriptVal, &GetGameList>("GetGameList");
	scriptInterface.RegisterFunction<CScriptVal, &GetBoardList>("GetBoardList");
	scriptInterface.RegisterFunction<CScriptVal, &LobbyGuiPollMessage>("LobbyGuiPollMessage");
	scriptInterface.RegisterFunction<void, std::string, &LobbySendMessage>("LobbySendMessage");
	scriptInterface.RegisterFunction<void, std::string, &LobbySetPlayerPresence>("LobbySetPlayerPresence");
	scriptInterface.RegisterFunction<void, std::string, &LobbySetNick>("LobbySetNick");
	scriptInterface.RegisterFunction<std::string, &LobbyGetNick>("LobbyGetNick");
	scriptInterface.RegisterFunction<void, std::string, std::string, &LobbyKick>("LobbyKick");
	scriptInterface.RegisterFunction<void, std::string, std::string, &LobbyBan>("LobbyBan");
	scriptInterface.RegisterFunction<std::string, std::string, &LobbyGetPlayerPresence>("LobbyGetPlayerPresence");
	scriptInterface.RegisterFunction<std::string, std::string, std::string, &EncryptPassword>("EncryptPassword");
	scriptInterface.RegisterFunction<bool, &IsRankedGame>("IsRankedGame");
	scriptInterface.RegisterFunction<void, bool, &SetRankedGame>("SetRankedGame");
#endif // CONFIG2_LOBBY
}
