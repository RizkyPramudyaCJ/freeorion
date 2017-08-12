#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "HumanClientApp.h"

#include "HumanClientFSM.h"
#include "../../UI/CUIControls.h"
#include "../../UI/CUIStyle.h"
#include "../../UI/MapWnd.h"
#include "../../UI/DesignWnd.h"
#include "../../UI/Hotkeys.h"
#include "../../UI/IntroScreen.h"
#include "../../UI/GalaxySetupWnd.h"
#include "../../UI/MultiplayerLobbyWnd.h"
#include "../../UI/SaveFileDialog.h"
#include "../../UI/ServerConnectWnd.h"
#include "../../UI/Sound.h"
#include "../../network/Message.h"
#include "../../network/Networking.h"
#include "../../network/ClientNetworking.h"
#include "../../util/i18n.h"
#include "../../util/LoggerWithOptionsDB.h"
#include "../../util/MultiplayerCommon.h"
#include "../../util/OptionsDB.h"
#include "../../util/Process.h"
#include "../../util/SaveGamePreviewUtils.h"
#include "../../util/Serialize.h"
#include "../../util/SitRepEntry.h"
#include "../../util/Directories.h"
#include "../../util/Version.h"
#include "../../universe/Planet.h"
#include "../../universe/Species.h"
#include "../../universe/Enums.h"
#include "../../Empire/Empire.h"
#include "../../combat/CombatLogManager.h"
#include "../../parse/Parse.h"

#include <GG/BrowseInfoWnd.h>
#include <GG/dialogs/ThreeButtonDlg.h>
#include <GG/Cursor.h>
#include <GG/utf8/checked.h>

#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/algorithm/string.hpp>

#include <chrono>
#include <thread>

#include <sstream>


namespace fs = boost::filesystem;

#ifdef ENABLE_CRASH_BACKTRACE
# include <signal.h>
# include <execinfo.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>

void SigHandler(int sig) {
    void* backtrace_buffer[100];
    int num;
    int fd;

    signal(sig, SIG_DFL);
    fd = open("crash.txt",O_WRONLY|O_CREAT|O_APPEND|O_SYNC,0666);
    if (fd != -1) {
        write(fd, "--- New crash backtrace begins here ---\n", 24);
        num = backtrace(backtrace_buffer, 100);
        backtrace_symbols_fd(backtrace_buffer, num, fd);
        backtrace_symbols_fd(backtrace_buffer, num, 2);
        close(fd);
    }

    // Now we try to display a MessageBox; this might fail and also
    // corrupt the heap, but since we're dying anyway that's no big
    // deal

    ClientUI::MessageBox("The client has just crashed!\nFile a bug report and\nattach the file called 'crash.txt'\nif necessary", true);

    raise(sig);
}
#endif //ENABLE_CRASH_BACKTRACE

namespace {
    const bool          INSTRUMENT_MESSAGE_HANDLING = false;

    // command-line options
    void AddOptions(OptionsDB& db) {
        db.Add("autosave.single-player",        UserStringNop("OPTIONS_DB_AUTOSAVE_SINGLE_PLAYER"),     true,   Validator<bool>());
        db.Add("autosave.multiplayer",          UserStringNop("OPTIONS_DB_AUTOSAVE_MULTIPLAYER"),       true,   Validator<bool>());
        db.Add("autosave.turns",                UserStringNop("OPTIONS_DB_AUTOSAVE_TURNS"),             1,      RangedValidator<int>(1, 50));
        db.Add("autosave.limit",                UserStringNop("OPTIONS_DB_AUTOSAVE_LIMIT"),             10,     RangedValidator<int>(1, 100));
        db.Add("autosave.initial-turn",         UserStringNop("OPTIONS_DB_AUTOSAVE_INITIAL_TURN"),      true,   Validator<bool>());
        db.Add("autosave.last-turn",            UserStringNop("OPTIONS_DB_AUTOSAVE_LAST_TURN"),         true,   Validator<bool>());
        db.Add("UI.swap-mouse-lr",              UserStringNop("OPTIONS_DB_UI_MOUSE_LR_SWAP"),           false);
        db.Add("UI.keypress-repeat-delay",      UserStringNop("OPTIONS_DB_KEYPRESS_REPEAT_DELAY"),      360,    RangedValidator<int>(0, 1000));
        db.Add("UI.keypress-repeat-interval",   UserStringNop("OPTIONS_DB_KEYPRESS_REPEAT_INTERVAL"),   20,     RangedValidator<int>(0, 1000));
        db.Add("UI.mouse-click-repeat-delay",   UserStringNop("OPTIONS_DB_MOUSE_REPEAT_DELAY"),         360,    RangedValidator<int>(0, 1000));
        db.Add("UI.mouse-click-repeat-interval",UserStringNop("OPTIONS_DB_MOUSE_REPEAT_INTERVAL"),      15,     RangedValidator<int>(0, 1000));

        Hotkey::AddHotkey("exit",       UserStringNop("HOTKEY_EXIT"),       GG::GGK_NONE,   GG::MOD_KEY_NONE);
        Hotkey::AddHotkey("quit",       UserStringNop("HOTKEY_QUIT"),       GG::GGK_NONE,   GG::MOD_KEY_NONE);
        Hotkey::AddHotkey("fullscreen", UserStringNop("HOTKEY_FULLSCREEN"), GG::GGK_RETURN, GG::MOD_KEY_ALT);
    }
    bool temp_bool = RegisterOptions(&AddOptions);

    /** These options can only be validated after the graphics system (SDL) is initialized,
        so that display size can be detected
     */
    const int DEFAULT_WIDTH = 1024;
    const int DEFAULT_HEIGHT = 768;
    const int DEFAULT_LEFT = static_cast<int>(SDL_WINDOWPOS_CENTERED);
    const int DEFAULT_TOP = 50;
    const int MIN_WIDTH = 800;
    const int MIN_HEIGHT = 600;

    /* Sets the value of options that need language-dependent default values.*/
    void SetStringtableDependentOptionDefaults() {
        if (GetOptionsDB().Get<std::string>("GameSetup.empire-name").empty())
            GetOptionsDB().Set("GameSetup.empire-name", UserString("DEFAULT_EMPIRE_NAME"));

        if (GetOptionsDB().Get<std::string>("GameSetup.player-name").empty())
            GetOptionsDB().Set("GameSetup.player-name", UserString("DEFAULT_PLAYER_NAME"));

        if (GetOptionsDB().Get<std::string>("multiplayersetup.player-name").empty())
            GetOptionsDB().Set("multiplayersetup.player-name", UserString("DEFAULT_PLAYER_NAME"));
    }

    std::string GetGLVersionString()
    { return boost::lexical_cast<std::string>(glGetString(GL_VERSION)); }

    static float stored_gl_version = -1.0f;  // to be replaced when gl version first checked

    float GetGLVersion() {
        if (stored_gl_version != -1.0f)
            return stored_gl_version;

        // get OpenGL version string and parse to get version number
        std::string gl_version_string = GetGLVersionString();

        float version_number = 0.0f;
        std::istringstream iss(gl_version_string);
        iss >> version_number;
        version_number += 0.05f;    // ensures proper rounding of 1.1 digit number

        stored_gl_version = version_number;

        return stored_gl_version;
    }

    void SetGLVersionDependentOptionDefaults() {
        // get OpenGL version string and parse to get version number
        float version_number = GetGLVersion();
        DebugLogger() << "OpenGL Version Number: " << DoubleToString(version_number, 2, false);    // combination of floating point precision and DoubleToString preferring to round down means the +0.05 is needed to round properly
        if (version_number < 2.0) {
            ErrorLogger() << "OpenGL Version is less than 2.0. FreeOrion may crash when trying to start a game.";
        }

        // only execute default option setting once
        if (GetOptionsDB().Get<bool>("checked-gl-version"))
            return;
        GetOptionsDB().Set<bool>("checked-gl-version", true);

        // if GL version is too low, set various map rendering options to
        // disabled, to hopefully improve frame rate.
        if (version_number < 2.0) {
            GetOptionsDB().Set<bool>("UI.galaxy-gas-background",        false);
            GetOptionsDB().Set<bool>("UI.galaxy-starfields",            false);
            GetOptionsDB().Set<bool>("UI.system-fog-of-war",            false);
        }
    }
}

void HumanClientApp::AddWindowSizeOptionsAfterMainStart(OptionsDB& db) {
    const int max_width_plus_one = HumanClientApp::MaximumPossibleWidth() + 1;
    const int max_height_plus_one = HumanClientApp::MaximumPossibleHeight() + 1;

    db.Add("app-width",             UserStringNop("OPTIONS_DB_APP_WIDTH"),             DEFAULT_WIDTH,       RangedValidator<int>(MIN_WIDTH, max_width_plus_one));
    db.Add("app-height",            UserStringNop("OPTIONS_DB_APP_HEIGHT"),            DEFAULT_HEIGHT,      RangedValidator<int>(MIN_HEIGHT, max_height_plus_one));
    db.Add("app-width-windowed",    UserStringNop("OPTIONS_DB_APP_WIDTH_WINDOWED"),    DEFAULT_WIDTH,       RangedValidator<int>(MIN_WIDTH, max_width_plus_one));
    db.Add("app-height-windowed",   UserStringNop("OPTIONS_DB_APP_HEIGHT_WINDOWED"),   DEFAULT_HEIGHT,      RangedValidator<int>(MIN_HEIGHT, max_height_plus_one));
    db.Add("app-left-windowed",     UserStringNop("OPTIONS_DB_APP_LEFT_WINDOWED"),     DEFAULT_LEFT,        OrValidator<int>( RangedValidator<int>(-max_width_plus_one, max_width_plus_one), DiscreteValidator<int>(DEFAULT_LEFT) ));
    db.Add("app-top-windowed",      UserStringNop("OPTIONS_DB_APP_TOP_WINDOWED"),      DEFAULT_TOP,         RangedValidator<int>(-max_height_plus_one, max_height_plus_one));
}

HumanClientApp::HumanClientApp(int width, int height, bool calculate_fps, const std::string& name,
                               int x, int y, bool fullscreen, bool fake_mode_change) :
    ClientApp(),
    SDLGUI(width, height, calculate_fps, name, x, y, fullscreen, fake_mode_change),
    m_single_player_game(true),
    m_game_started(false),
    m_connected(false),
    m_auto_turns(0),
    m_have_window_focus(true),
    m_game_saves_in_progress()
{
#ifdef ENABLE_CRASH_BACKTRACE
    signal(SIGSEGV, SigHandler);
#endif
#ifdef FREEORION_MACOSX
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
#endif
    m_fsm.reset(new HumanClientFSM(*this));

    const std::string HUMAN_CLIENT_LOG_FILENAME((GetUserDataDir() / "freeorion.log").string());

    // Force the log threshold if requested.
    auto force_log_level = GetOptionsDB().Get<std::string>("log-level");
    if (!force_log_level.empty())
        OverrideAllLoggersThresholds(to_LogLevel(force_log_level));

    InitLoggingSystem(HUMAN_CLIENT_LOG_FILENAME, "Client");
    InitLoggingOptionsDBSystem();

    // Force loggers to always appear in the config.xml and OptionsWnd even before their
    // initialization on first use.
    // This is not needed for the loggers to work correctly.
    // this is not needed for the loggers to automatically be added to the config.xml on
    // first use.
    // This only needs to be done in one of the executables connected to the same config.xml
    RegisterLoggerWithOptionsDB("ai", true);
    RegisterLoggerWithOptionsDB("client", true);
    RegisterLoggerWithOptionsDB("server", true);
    RegisterLoggerWithOptionsDB("combat_log");
    RegisterLoggerWithOptionsDB("combat");
    RegisterLoggerWithOptionsDB("effects");
    RegisterLoggerWithOptionsDB("FSM");
    RegisterLoggerWithOptionsDB("network");
    RegisterLoggerWithOptionsDB("python");

    InfoLogger() << FreeOrionVersionString();

    try {
        InfoLogger() << "GL Version String: " << GetGLVersionString();
    } catch (...) {
        ErrorLogger() << "Unable to get GL Version String?";
    }

    LogDependencyVersions();

    SetStyleFactory(std::make_shared<CUIStyle>());

    SetMinDragTime(0);

    bool inform_user_sound_failed(false);
    try {
        if (GetOptionsDB().Get<bool>("UI.sound.enabled") || GetOptionsDB().Get<bool>("UI.sound.music-enabled"))
            Sound::GetSound().Enable();

        if ((GetOptionsDB().Get<bool>("UI.sound.music-enabled")))
            Sound::GetSound().PlayMusic(GetOptionsDB().Get<std::string>("UI.sound.bg-music"), -1);

        Sound::GetSound().SetMusicVolume(GetOptionsDB().Get<int>("UI.sound.music-volume"));
        Sound::GetSound().SetUISoundsVolume(GetOptionsDB().Get<int>("UI.sound.volume"));
    } catch (Sound::InitializationFailureException const &) {
        inform_user_sound_failed = true;
    }

    m_ui.reset(new ClientUI());

    EnableFPS();
    UpdateFPSLimit();
    GetOptionsDB().OptionChangedSignal("show-fps").connect(
        boost::bind(&HumanClientApp::UpdateFPSLimit, this));
    GetOptionsDB().OptionChangedSignal("max-fps").connect(
        boost::bind(&HumanClientApp::UpdateFPSLimit, this));

    std::shared_ptr<GG::BrowseInfoWnd> default_browse_info_wnd(
        GG::Wnd::Create<GG::TextBoxBrowseInfoWnd>(
            GG::X(400), ClientUI::GetFont(),
            GG::Clr(0, 0, 0, 200), ClientUI::WndOuterBorderColor(), ClientUI::TextColor(),
            GG::FORMAT_LEFT | GG::FORMAT_WORDBREAK, 1));
    GG::Wnd::SetDefaultBrowseInfoWnd(default_browse_info_wnd);

    std::shared_ptr<GG::Texture> cursor_texture = m_ui->GetTexture(ClientUI::ArtDir() / "cursors" / "default_cursor.png");
    SetCursor(std::make_shared<GG::TextureCursor>(cursor_texture, GG::Pt(GG::X(6), GG::Y(3))));
    RenderCursor(true);

    EnableKeyPressRepeat(GetOptionsDB().Get<int>("UI.keypress-repeat-delay"),
                         GetOptionsDB().Get<int>("UI.keypress-repeat-interval"));
    EnableMouseButtonDownRepeat(GetOptionsDB().Get<int>("UI.mouse-click-repeat-delay"),
                                GetOptionsDB().Get<int>("UI.mouse-click-repeat-interval"));
    EnableModalAcceleratorSignals(true);

    WindowResizedSignal.connect(
        boost::bind(&HumanClientApp::HandleWindowResize, this, _1, _2));
    FocusChangedSignal.connect(
        boost::bind(&HumanClientApp::HandleFocusChange, this, _1));
    WindowMovedSignal.connect(
        boost::bind(&HumanClientApp::HandleWindowMove, this, _1, _2));
    WindowClosingSignal.connect(
        boost::bind(&HumanClientApp::HandleAppQuitting, this));
    AppQuittingSignal.connect(
        boost::bind(&HumanClientApp::HandleAppQuitting, this));

    SetStringtableDependentOptionDefaults();
    SetGLVersionDependentOptionDefaults();

    this->SetMouseLRSwapped(GetOptionsDB().Get<bool>("UI.swap-mouse-lr"));

    std::map<std::string, std::map<int, int>> named_key_maps;
    parse::keymaps(named_key_maps);
    TraceLogger() << "Keymaps:";
    for (std::map<std::string, std::map<int, int>>::value_type& km : named_key_maps) {
        TraceLogger() << "Keymap name = \"" << km.first << "\"";
        for (std::map<int, int>::value_type& keys : km.second)
            TraceLogger() << "    " << char(keys.first) << " : " << char(keys.second);
    }
    std::map<std::string, std::map<int, int>>::const_iterator km_it = named_key_maps.find("TEST");
    if (km_it != named_key_maps.end()) {
        const std::map<int, int> int_key_map = km_it->second;
        std::map<GG::Key, GG::Key> key_map;
        for (const std::map<int, int>::value_type& int_key : int_key_map)
        { key_map[GG::Key(int_key.first)] = GG::Key(int_key.second); }
        this->SetKeyMap(key_map);
    }

    ConnectKeyboardAcceleratorSignals();

    InitAutoTurns(GetOptionsDB().Get<int>("auto-advance-n-turns"));

    if (fake_mode_change && !FramebuffersAvailable()) {
        ErrorLogger() << "Requested fake mode changes, but the framebuffer opengl extension is not available. Ignoring.";
    }

    // Placed after mouse initialization.
    if (inform_user_sound_failed)
        ClientUI::MessageBox(UserString("ERROR_SOUND_INITIALIZATION_FAILED"), false);

    // Register LinkText tags with GG::Font
    RegisterLinkTags();

    m_fsm->initiate();
}

void HumanClientApp::ConnectKeyboardAcceleratorSignals() {
    // Add global hotkeys
    HotkeyManager *hkm = HotkeyManager::GetManager();

    hkm->Connect(boost::bind(&HumanClientApp::HandleHotkeyExitApp, this),          "exit",
                 NoModalWndsOpenCondition);
    hkm->Connect(boost::bind(&HumanClientApp::HandleHotkeyResetGame, this),        "quit",
                 NoModalWndsOpenCondition);
    hkm->Connect(boost::bind(&HumanClientApp::ToggleFullscreen, this), "fullscreen",
                 NoModalWndsOpenCondition);

    hkm->RebuildShortcuts();
}

HumanClientApp::~HumanClientApp() {
    m_networking->DisconnectFromServer();
    m_server_process.RequestTermination();
    DebugLogger() << "HumanClientApp exited cleanly.";
}

bool HumanClientApp::SinglePlayerGame() const
{ return m_single_player_game; }

bool HumanClientApp::CanSaveNow() const {
    // only host can save in multiplayer
    if (!SinglePlayerGame() && !Networking().PlayerIsHost(PlayerID()))
        return false;

    // can't save while AIs are playing their turns...
    for (const std::map<int, PlayerInfo>::value_type& entry : m_player_info) {
        const PlayerInfo& info = entry.second;
        if (info.client_type != Networking::CLIENT_TYPE_AI_PLAYER)
            continue;   // only care about AIs

        std::map<int, Message::PlayerStatus>::const_iterator
            status_it = m_player_status.find(entry.first);

        if (status_it == this->m_player_status.end()) {
            return false;  // missing status for AI; can't assume it's ready
        }
        if (status_it->second != Message::WAITING) {
            return false;
        }
    }

    return true;
}

void HumanClientApp::SetSinglePlayerGame(bool sp/* = true*/)
{ m_single_player_game = sp; }

namespace {
    std::string ServerClientExe() {
#ifdef FREEORION_WIN32
        return PathString(GetBinDir() / "freeoriond.exe");
#else
        return (GetBinDir() / "freeoriond").string();
#endif
    }
}

#ifdef FREEORION_MACOSX
#include <stdlib.h>
#endif

void HumanClientApp::StartServer() {
    std::string SERVER_CLIENT_EXE = ServerClientExe();
    DebugLogger() << "HumanClientApp::StartServer: " << SERVER_CLIENT_EXE;

#ifdef FREEORION_MACOSX
    // On OSX set environment variable DYLD_LIBRARY_PATH to python framework folder
    // bundled with app, so the dynamic linker uses the bundled python library.
    // Otherwise the dynamic linker will look for a correct python lib in system
    // paths, and if it can't find it, throw an error and terminate!
    // Setting environment variable here, spawned child processes will inherit it.
    setenv("DYLD_LIBRARY_PATH", GetPythonHome().string().c_str(), 1);
#endif

    std::vector<std::string> args;
    std::string ai_config = GetOptionsDB().Get<std::string>("ai-config");
    std::string ai_path = GetOptionsDB().Get<std::string>("ai-path");
    args.push_back("\"" + SERVER_CLIENT_EXE + "\"");
    args.push_back("--resource-dir");
    args.push_back("\"" + GetOptionsDB().Get<std::string>("resource-dir") + "\"");

    auto force_log_level = GetOptionsDB().Get<std::string>("log-level");
    if (!force_log_level.empty()) {
        args.push_back("--log-level");
        args.push_back(GetOptionsDB().Get<std::string>("log-level"));
    }

    if (ai_path != GetOptionsDB().GetDefaultValueString("ai-path")) {
        args.push_back("--ai-path");
        args.push_back(ai_path);
        DebugLogger() << "ai-path set to '" << ai_path << "'";
    }
    if (!ai_config.empty()) {
        args.push_back("--ai-config");
        args.push_back(ai_config);
        DebugLogger() << "ai-config set to '" << ai_config << "'";
    } else {
        DebugLogger() << "ai-config not set.";
    }
    if (m_single_player_game) {
        args.push_back("--singleplayer");
    }
    m_server_process = Process(SERVER_CLIENT_EXE, args);
}

void HumanClientApp::FreeServer() {
    m_server_process.Free();
    m_networking->SetPlayerID(Networking::INVALID_PLAYER_ID);
    m_networking->SetHostPlayerID(Networking::INVALID_PLAYER_ID);
    SetEmpireID(ALL_EMPIRES);
}

void HumanClientApp::NewSinglePlayerGame(bool quickstart) {
    if (!GetOptionsDB().Get<bool>("force-external-server")) {
        m_single_player_game = true;
        try {
            StartServer();
        } catch (const std::runtime_error& err) {
            ErrorLogger() << "HumanClientApp::NewSinglePlayerGame : Couldn't start server.  Got error message: " << err.what();
            ClientUI::MessageBox(UserString("SERVER_WONT_START"), true);
            return;
        }
    }

    bool ended_with_ok = false;
    std::vector<std::pair<std::string, std::string>> game_rules = GetGameRules().GetRulesAsStrings();
    if (!quickstart) {
        auto galaxy_wnd = GG::Wnd::Create<GalaxySetupWnd>();
        galaxy_wnd->Run();
        ended_with_ok = galaxy_wnd->EndedWithOk();
        if (ended_with_ok)
            game_rules = galaxy_wnd->GetRulesAsStrings();
    }

    m_connected = m_networking->ConnectToLocalHostServer();
    if (!m_connected) {
        ResetToIntro(true);
        ClientUI::MessageBox(UserString("ERR_CONNECT_TIMED_OUT"), true);
        return;
    }

    if (quickstart || ended_with_ok) {

        SinglePlayerSetupData setup_data;
        setup_data.m_new_game = true;
        setup_data.m_filename.clear();  // not used for new game

        // get values stored in options from previous time game was run or
        // from just having run GalaxySetupWnd

        // GalaxySetupData
        setup_data.m_seed =             GetOptionsDB().Get<std::string>("GameSetup.seed");
        setup_data.m_size =             GetOptionsDB().Get<int>("GameSetup.stars");
        setup_data.m_shape =            GetOptionsDB().Get<Shape>("GameSetup.galaxy-shape");
        setup_data.m_age =              GetOptionsDB().Get<GalaxySetupOption>("GameSetup.galaxy-age");
        setup_data.m_starlane_freq =    GetOptionsDB().Get<GalaxySetupOption>("GameSetup.starlane-frequency");
        setup_data.m_planet_density =   GetOptionsDB().Get<GalaxySetupOption>("GameSetup.planet-density");
        setup_data.m_specials_freq =    GetOptionsDB().Get<GalaxySetupOption>("GameSetup.specials-frequency");
        setup_data.m_monster_freq =     GetOptionsDB().Get<GalaxySetupOption>("GameSetup.monster-frequency");
        setup_data.m_native_freq =      GetOptionsDB().Get<GalaxySetupOption>("GameSetup.native-frequency");
        setup_data.m_ai_aggr =          GetOptionsDB().Get<Aggression>("GameSetup.ai-aggression");
        setup_data.m_game_rules =       game_rules;


        // SinglePlayerSetupData contains a map of PlayerSetupData, for
        // the human and AI players.  Need to compile this information
        // from the specified human options and number of requested AIs

        // Human player setup data
        PlayerSetupData human_player_setup_data;
        human_player_setup_data.m_player_name = GetOptionsDB().Get<std::string>("GameSetup.player-name");
        human_player_setup_data.m_empire_name = GetOptionsDB().Get<std::string>("GameSetup.empire-name");

        // DB stores index into array of available colours, so need to get that array to look up value of index.
        // if stored value is invalid, use a default colour
        const std::vector<GG::Clr>& empire_colours = EmpireColors();
        int colour_index = GetOptionsDB().Get<int>("GameSetup.empire-color");
        if (colour_index >= 0 && colour_index < static_cast<int>(empire_colours.size()))
            human_player_setup_data.m_empire_color = empire_colours[colour_index];
        else
            human_player_setup_data.m_empire_color = GG::CLR_GREEN;

        human_player_setup_data.m_starting_species_name = GetOptionsDB().Get<std::string>("GameSetup.starting-species");
        if (human_player_setup_data.m_starting_species_name == "1")
            human_player_setup_data.m_starting_species_name = "SP_HUMAN";   // kludge / bug workaround for bug with options storage and retreival.  Empty-string options are stored, but read in as "true" boolean, and converted to string equal to "1"

        if (human_player_setup_data.m_starting_species_name != "RANDOM" &&
            !GetSpecies(human_player_setup_data.m_starting_species_name))
        {
            const SpeciesManager& sm = GetSpeciesManager();
            if (sm.NumPlayableSpecies() < 1)
                human_player_setup_data.m_starting_species_name.clear();
            else
                human_player_setup_data.m_starting_species_name = sm.playable_begin()->first;
        }

        human_player_setup_data.m_save_game_empire_id = ALL_EMPIRES; // not used for new games
        human_player_setup_data.m_client_type = Networking::CLIENT_TYPE_HUMAN_PLAYER;

        // add to setup data players
        setup_data.m_players.push_back(human_player_setup_data);


        // AI player setup data.  One entry for each requested AI
        int num_AIs = GetOptionsDB().Get<int>("GameSetup.ai-players");
        for (int ai_i = 1; ai_i <= num_AIs; ++ai_i) {
            PlayerSetupData ai_setup_data;

            ai_setup_data.m_player_name = "AI_" + std::to_string(ai_i);
            ai_setup_data.m_empire_name.clear();                // leave blank, to be set by server in Universe::GenerateEmpires
            ai_setup_data.m_empire_color = GG::CLR_ZERO;        // to be set by server
            ai_setup_data.m_starting_species_name.clear();      // leave blank, to be set by server
            ai_setup_data.m_save_game_empire_id = ALL_EMPIRES;  // not used for new games
            ai_setup_data.m_client_type = Networking::CLIENT_TYPE_AI_PLAYER;

            setup_data.m_players.push_back(ai_setup_data);
        }

        m_networking->SendMessage(HostSPGameMessage(setup_data));
        m_fsm->process_event(HostSPGameRequested());

    } else {
        ErrorLogger() << "HumanClientApp::NewSinglePlayerGame failed to start new game, killing server.";
        ResetToIntro(true);
    }
}

void HumanClientApp::MultiPlayerGame() {
    if (m_networking->IsConnected()) {
        ErrorLogger() << "HumanClientApp::MultiPlayerGame aborting because already connected to a server";
        return;
    }

    auto server_connect_wnd = GG::Wnd::Create<ServerConnectWnd>();
    server_connect_wnd->Run();

    std::string server_name = server_connect_wnd->Result().second;

    if (server_name.empty())
        return;

    if (server_name == "HOST GAME SELECTED") {
        if (!GetOptionsDB().Get<bool>("force-external-server")) {
            m_single_player_game = false;
            try {
                StartServer();
                FreeServer();
            } catch (const std::runtime_error& err) {
                ErrorLogger() << "Couldn't start server.  Got error message: " << err.what();
                ClientUI::MessageBox(UserString("SERVER_WONT_START"), true);
                return;
            }
            server_name = "localhost";
        }
        server_name = GetOptionsDB().Get<std::string>("external-server-address");
    }


    m_connected = m_networking->ConnectToServer(server_name);
    if (!m_connected) {
        ClientUI::MessageBox(UserString("ERR_CONNECT_TIMED_OUT"), true);
        if (server_connect_wnd->Result().second == "HOST GAME SELECTED")
            ResetToIntro(true);
        return;
    }

    if (server_connect_wnd->Result().second == "HOST GAME SELECTED") {
        m_networking->SendMessage(HostMPGameMessage(server_connect_wnd->Result().first));
        m_fsm->process_event(HostMPGameRequested());
    } else {
        m_networking->SendMessage(JoinGameMessage(server_connect_wnd->Result().first, Networking::CLIENT_TYPE_HUMAN_PLAYER));
        m_fsm->process_event(JoinMPGameRequested());
    }
}

void HumanClientApp::StartMultiPlayerGameFromLobby()
{ m_fsm->process_event(StartMPGameClicked()); }

void HumanClientApp::CancelMultiplayerGameFromLobby()
{ m_fsm->process_event(CancelMPGameClicked()); }

void HumanClientApp::SaveGame(const std::string& filename) {
    m_game_saves_in_progress.push(filename);

    // Start a save if there is not one in progress
    if (m_game_saves_in_progress.size() > 1) {
        DebugLogger() << "Add pending save to queue.";
        return;
    }

    m_networking->SendMessage(HostSaveGameInitiateMessage(filename));
    DebugLogger() << "Sent save initiate message to server.";
}

void HumanClientApp::SaveGameCompleted() {
    m_game_saves_in_progress.pop();

    // Either indicate that all save are completed or start the next save.
    if (m_game_saves_in_progress.empty()) {
        DebugLogger() << "Save games completed.";
        SaveGamesCompletedSignal();
    } else {
        m_networking->SendMessage(HostSaveGameInitiateMessage(m_game_saves_in_progress.front()));
        DebugLogger() << "Sent next save initiate message to server.";
    }
}

void HumanClientApp::LoadSinglePlayerGame(std::string filename/* = ""*/) {
    DebugLogger() << "HumanClientApp::LoadSinglePlayerGame";

    if (!filename.empty()) {
        if (!exists(FilenameToPath(filename))) {
            std::string msg = "HumanClientApp::LoadSinglePlayerGame() given a nonexistent file \""
                            + filename + "\" to load; aborting.";
            DebugLogger() << msg;
            std::cerr << msg << '\n';
            abort();
        }
    } else {
        try {
            auto sfd = GG::Wnd::Create<SaveFileDialog>(SP_SAVE_FILE_EXTENSION, true);
            sfd->Run();
            if (!sfd->Result().empty())
                filename = sfd->Result();
        } catch (const std::exception& e) {
            ClientUI::MessageBox(e.what(), true);
        }
    }

    if (filename.empty()) {
        DebugLogger() << "HumanClientApp::LoadSinglePlayerGame has empty filename. Aborting load.";
        return;
    }

    // end any currently-playing game before loading new one
    if (m_game_started) {
        ResetToIntro(false);
        // delay to make sure old game is fully cleaned up before attempting to start a new one
        std::this_thread::sleep_for(std::chrono::seconds(3));
    } else {
        DebugLogger() << "HumanClientApp::LoadSinglePlayerGame() not already in a game, so don't need to end it";
    }

    if (!GetOptionsDB().Get<bool>("force-external-server")) {
        m_single_player_game = true;
        DebugLogger() << "HumanClientApp::LoadSinglePlayerGame() Starting server";
        StartServer();
        DebugLogger() << "HumanClientApp::LoadSinglePlayerGame() Server started";
    } else {
        DebugLogger() << "HumanClientApp::LoadSinglePlayerGame() assuming external server will be available";
    }

    DebugLogger() << "HumanClientApp::LoadSinglePlayerGame() Connecting to server";
    m_connected = m_networking->ConnectToLocalHostServer();
    if (!m_connected) {
        ResetToIntro(true);
        ClientUI::MessageBox(UserString("ERR_CONNECT_TIMED_OUT"), true);
        return;
    }

    m_networking->SetPlayerID(Networking::INVALID_PLAYER_ID);
    m_networking->SetHostPlayerID(Networking::INVALID_PLAYER_ID);
    SetEmpireID(ALL_EMPIRES);

    SinglePlayerSetupData setup_data;
    // leving GalaxySetupData information default / blank : not used when loading a game
    setup_data.m_new_game = false;
    setup_data.m_filename = filename;
    // leving setup_data.m_players empty : not specified when loading a game, as server will generate from save file


    m_networking->SendMessage(HostSPGameMessage(setup_data));
    m_fsm->process_event(HostSPGameRequested());
}

void HumanClientApp::RequestSavePreviews(const std::string& directory, PreviewInformation& previews) {
    //TraceLogger() << "HumanClientApp::RequestSavePreviews directory: " << directory << " valid UTF-8: " << utf8::is_valid(directory.begin(), directory.end()) << std::endl;
    DebugLogger() << "HumanClientApp::RequestSavePreviews directory: " << directory << " valid UTF-8: " << utf8::is_valid(directory.begin(), directory.end());

    std::string  generic_directory = directory;//PathString(fs::path(directory));
    if (!m_networking->IsConnected()) {
        DebugLogger() << "HumanClientApp::RequestSavePreviews: No game running. Start a server for savegame queries.";

        m_single_player_game = true;
        StartServer();

        DebugLogger() << "HumanClientApp::RequestSavePreviews Connecting to server";
        m_connected = m_networking->ConnectToLocalHostServer();
        if (!m_connected) {
            ResetToIntro(true);
            ClientUI::MessageBox(UserString("ERR_CONNECT_TIMED_OUT"), true);
            return;
        }

        // This will only generate an error message and use the server's config.xml
        // because there is no host client for this temporary server.
        SendLoggingConfigToServer();
    }
    DebugLogger() << "HumanClientApp::RequestSavePreviews Requesting previews for " << generic_directory;
    const auto response = m_networking->SendSynchronousMessage(RequestSavePreviewsMessage(generic_directory));
    if (response && response->Type() == Message::DISPATCH_SAVE_PREVIEWS) {
        ExtractDispatchSavePreviewsMessageData(*response, previews);
        DebugLogger() << "HumanClientApp::RequestSavePreviews Got " << previews.previews.size() << " previews.";
    } else {
        ErrorLogger() << "HumanClientApp::RequestSavePreviews: Wrong response type from server: " << response->Type();
    }
}

std::pair<int, int> HumanClientApp::GetWindowLeftTop() {
    int left(0), top(0);

    left = GetOptionsDB().Get<int>("app-left-windowed");
    top = GetOptionsDB().Get<int>("app-top-windowed");

    // clamp to edges to avoid weird bug with maximizing windows setting their
    // left and top to -9 which lead to weird issues when attmepting to recreate
    // the window at those positions next execution
    if (std::abs(left) < 10)
        left = 0;
    if (std::abs(top) < 10)
        top = 0;

    return {left, top};
}

std::pair<int, int> HumanClientApp::GetWindowWidthHeight() {
    int width(800), height(600);

    bool fullscreen = GetOptionsDB().Get<bool>("fullscreen");
    if (!fullscreen) {
        width = GetOptionsDB().Get<int>("app-width-windowed");
        height = GetOptionsDB().Get<int>("app-height-windowed");
        return {width, height};
    }

    bool reset_fullscreen = GetOptionsDB().Get<bool>("reset-fullscreen-size");
    if (!reset_fullscreen) {
        width = GetOptionsDB().Get<int>("app-width");
        height = GetOptionsDB().Get<int>("app-height");
        return {width, height};
    }

    GetOptionsDB().Set<bool>("reset-fullscreen-size", false);
    GG::Pt default_resolution = GetDefaultResolutionStatic(GetOptionsDB().Get<int>("fullscreen-monitor-id"));
    GetOptionsDB().Set("app-width", Value(default_resolution.x));
    GetOptionsDB().Set("app-height", Value(default_resolution.y));
    GetOptionsDB().Commit();
    return {Value(default_resolution.x), Value(default_resolution.y)};
}

void HumanClientApp::Reinitialize() {
    bool fullscreen = GetOptionsDB().Get<bool>("fullscreen");
    bool fake_mode_change = GetOptionsDB().Get<bool>("fake-mode-change");
    std::pair<int, int> size = GetWindowWidthHeight();

    bool fullscreen_transition = Fullscreen() != fullscreen;
    GG::X old_width = AppWidth();
    GG::Y old_height = AppHeight();

    SetVideoMode(GG::X(size.first), GG::Y(size.second), fullscreen, fake_mode_change);
    if (fullscreen_transition) {
        FullscreenSwitchSignal(fullscreen); // after video mode is changed but before DoLayout() calls
    } else if (fullscreen &&
               (old_width != size.first || old_height != size.second) &&
               GetOptionsDB().Get<bool>("UI.auto-reposition-windows"))
    {
        // Reposition windows if in fullscreen mode... handled here instead of
        // HandleWindowResize() because the prev. fullscreen resolution is only
        // available here.
        RepositionWindowsSignal();
    }

    // HandleWindowResize is already called via this signal sent from
    // SDLGUI::HandleSystemEvents() when in windowed mode.  This sends the
    // signal (and hence calls HandleWindowResize()) when in fullscreen mode,
    // making the signal more consistent...
    if (fullscreen) {
        WindowResizedSignal(GG::X(size.first), GG::Y(size.second));
    }
}

float HumanClientApp::GLVersion() const
{ return GetGLVersion(); }

void HumanClientApp::StartTurn() {
    DebugLogger() << "HumanClientApp::StartTurn";

    if (const Empire* empire = GetEmpire(EmpireID())) {
        double RP = empire->ResourceOutput(RE_RESEARCH);
        double PP = empire->ResourceOutput(RE_INDUSTRY);
        int turn_number = CurrentTurn();
        float ratio = (RP/(PP+0.0001));
        const GG::Clr color = empire->Color();
        DebugLogger() << "Current Output (turn " << turn_number << ") RP/PP: " << ratio << " (" << RP << "/" << PP << ")";
        DebugLogger() << "EmpireColors: " << static_cast<int>(color.r)
                      << " " << static_cast<int>(color.g)
                      << " " << static_cast<int>(color.b)
                      << " " << static_cast<int>(color.a);
    }

    ClientApp::StartTurn();
    m_fsm->process_event(TurnEnded());
}

void HumanClientApp::HandleSystemEvents() {
    try {
        SDLGUI::HandleSystemEvents();
    } catch (const utf8::invalid_utf8& e) {
        ErrorLogger() << "UTF-8 error handling system event: " << e.what();
    }
    if (m_connected && !m_networking->IsConnected()) {
        m_connected = false;
        DisconnectedFromServer();
    } else if (auto msg = Networking().GetMessage()) {
        HandleMessage(*msg);
    }
}

void HumanClientApp::RenderBegin() {
    SDLGUI::RenderBegin();
    Sound::GetSound().DoFrame();
}

void HumanClientApp::HandleMessage(Message& msg) {
    if (INSTRUMENT_MESSAGE_HANDLING)
        std::cerr << "HumanClientApp::HandleMessage(" << msg.Type() << ")\n";

    switch (msg.Type()) {
    case Message::ERROR_MSG:                m_fsm->process_event(Error(msg));                   break;
    case Message::HOST_MP_GAME:             m_fsm->process_event(HostMPGame(msg));              break;
    case Message::HOST_SP_GAME:             m_fsm->process_event(HostSPGame(msg));              break;
    case Message::JOIN_GAME:                m_fsm->process_event(JoinGame(msg));                break;
    case Message::HOST_ID:                  m_fsm->process_event(HostID(msg));                  break;
    case Message::LOBBY_UPDATE:             m_fsm->process_event(LobbyUpdate(msg));             break;
    case Message::SAVE_GAME_DATA_REQUEST:   m_fsm->process_event(SaveGameDataRequest(msg));     break;
    case Message::SAVE_GAME_COMPLETE:       m_fsm->process_event(SaveGameComplete(msg));        break;
    case Message::CHECKSUM:                 m_fsm->process_event(CheckSum(msg));                break;
    case Message::GAME_START:               m_fsm->process_event(GameStart(msg));               break;
    case Message::TURN_UPDATE:              m_fsm->process_event(TurnUpdate(msg));              break;
    case Message::TURN_PARTIAL_UPDATE:      m_fsm->process_event(TurnPartialUpdate(msg));       break;
    case Message::TURN_PROGRESS:            m_fsm->process_event(TurnProgress(msg));            break;
    case Message::PLAYER_STATUS:            m_fsm->process_event(::PlayerStatus(msg));          break;
    case Message::PLAYER_CHAT:              m_fsm->process_event(PlayerChat(msg));              break;
    case Message::DIPLOMACY:                m_fsm->process_event(Diplomacy(msg));               break;
    case Message::DIPLOMATIC_STATUS:        m_fsm->process_event(DiplomaticStatusUpdate(msg));  break;
    case Message::END_GAME:                 m_fsm->process_event(::EndGame(msg));               break;

    case Message::DISPATCH_COMBAT_LOGS:     m_fsm->process_event(DispatchCombatLogs(msg));       break;
    default:
        ErrorLogger() << "HumanClientApp::HandleMessage : Received an unknown message type \"" << msg.Type() << "\".";
    }
}

void HumanClientApp::HandleSaveGameDataRequest() {
    if (INSTRUMENT_MESSAGE_HANDLING)
        std::cerr << "HumanClientApp::HandleSaveGameDataRequest(" << Message::SAVE_GAME_DATA_REQUEST << ")\n";
    SaveGameUIData ui_data;
    m_ui->GetSaveGameUIData(ui_data);
    m_networking->SendMessage(ClientSaveDataMessage(Orders(), ui_data));
}

void HumanClientApp::UpdateCombatLogs(const Message& msg){
    DebugLogger() << "HCL Update Combat Logs";

    // Unpack the combat logs from the message
    std::vector<std::pair<int, CombatLog>> logs;
    ExtractDispatchCombatLogsMessageData(msg, logs);

    // Update the combat log manager with the completed logs.
    for (std::vector<std::pair<int, CombatLog>>::const_iterator it = logs.begin();
         it != logs.end(); ++it)
    {
        GetCombatLogManager().CompleteLog(it->first, it->second);
    }
}

void HumanClientApp::ChangeLoggerThreshold(const std::string& option_name, LogLevel option_value) {
    // Update the logger threshold in OptionsDB
    ChangeLoggerThresholdInOptionsDB(option_name, option_value);

    SendLoggingConfigToServer();
}

void HumanClientApp::SendLoggingConfigToServer() {
    // If not host then done.
    if (!m_networking->PlayerIsHost(Networking().PlayerID()))
        return;

    // Host updates the server
    const auto sources = LoggerOptionsLabelsAndLevels(LoggerTypes::both);

    m_networking->SendMessage(LoggerConfigMessage(PlayerID(), sources));
}

void HumanClientApp::HandleWindowMove(GG::X w, GG::Y h) {
    if (!Fullscreen()) {
        GetOptionsDB().Set<int>("app-left-windowed", Value(w));
        GetOptionsDB().Set<int>("app-top-windowed", Value(h));
        GetOptionsDB().Commit();
    }
}

void HumanClientApp::HandleWindowResize(GG::X w, GG::Y h) {
    if (ClientUI* ui = ClientUI::GetClientUI()) {
        if (auto&& map_wnd = ui->GetMapWnd())
            map_wnd->DoLayout();
        if (auto&& intro_screen = ui->GetIntroScreen()) {
            intro_screen->Resize(GG::Pt(w, h));
            intro_screen->DoLayout();
        }
    }

    if (!GetOptionsDB().Get<bool>("fullscreen") &&
         (GetOptionsDB().Get<int>("app-width-windowed") != w ||
          GetOptionsDB().Get<int>("app-height-windowed") != h))
    {
        if (GetOptionsDB().Get<bool>("UI.auto-reposition-windows")) {
            // Reposition windows if in windowed mode.
            RepositionWindowsSignal();
        }
        // store resize if window is not full-screen (so that fullscreen
        // resolution doesn't overwrite windowed resolution)
        GetOptionsDB().Set<int>("app-width-windowed", Value(w));
        GetOptionsDB().Set<int>("app-height-windowed", Value(h));
    }

    glViewport(0, 0, Value(w), Value(h));

    GetOptionsDB().Commit();
}

void HumanClientApp::HandleFocusChange(bool gained_focus) {
    DebugLogger() << "HumanClientApp::HandleFocusChange("
                  << (gained_focus ? "Gained Focus" : "Lost Focus")
                  << ")";

    m_have_window_focus = gained_focus;

    // limit rendering frequency when defocused to limit CPU use, and disable sound
    if (!m_have_window_focus) {
        if (GetOptionsDB().Get<bool>("limit-fps-no-focus"))
            this->SetMaxFPS(GetOptionsDB().Get<double>("max-fps-no_focus"));
        else
            this->SetMaxFPS(0.0);

        if (GetOptionsDB().Get<bool>("UI.sound.music-enabled"))
            Sound::GetSound().PauseMusic();
    }
    else {
        if (GetOptionsDB().Get<bool>("limit-fps"))
            this->SetMaxFPS(GetOptionsDB().Get<double>("max-fps"));
        else
            this->SetMaxFPS(0.0);

        if (GetOptionsDB().Get<bool>("UI.sound.music-enabled"))
            Sound::GetSound().ResumeMusic();
    }

    CancelDragDrop();
    ClearEventState();
}

void HumanClientApp::HandleAppQuitting() {
    DebugLogger() << "HumanClientApp::HandleAppQuitting()";
    ExitApp();
}

bool HumanClientApp::HandleHotkeyResetGame() {
    DebugLogger() << "HumanClientApp::HandleHotkeyResetGame()";
    ResetToIntro(false);
    return true;
}

bool HumanClientApp::HandleHotkeyExitApp() {
    DebugLogger() << "HumanClientApp::HandleHotkeyExitApp()";
    HandleAppQuitting();
    // Not reached, but required for HotkeyManager::Connect()
    return true;
}

bool HumanClientApp::ToggleFullscreen() {
    bool fs = GetOptionsDB().Get<bool>("fullscreen");
    GetOptionsDB().Set<bool>("fullscreen", !fs);
    Reinitialize();
    return true;
}

void HumanClientApp::StartGame(bool is_new_game) {
    m_game_started = true;

    if (auto&& map_wnd = ClientUI::GetClientUI()->GetMapWnd())
        map_wnd->ResetEmpireShown();

    ClientUI::GetClientUI()->GetShipDesignManager()->StartGame(EmpireID(), is_new_game);

    UpdateCombatLogManager();
}

void HumanClientApp::HandleTurnUpdate()
{ UpdateCombatLogManager(); }

void HumanClientApp::UpdateCombatLogManager() {
    boost::optional<std::vector<int>> incomplete_ids = GetCombatLogManager().IncompleteLogIDs();
    if (incomplete_ids)
        m_networking->SendMessage(RequestCombatLogsMessage(*incomplete_ids));
}

namespace {
    void RemoveOldestFiles(int files_limit, boost::filesystem::path& p) {
        using namespace boost::filesystem;
        try {
            if (!is_directory(p))
                return;
            if (files_limit < 0)
                return;

            std::multimap<std::time_t, path> files_by_write_time;

            for (directory_iterator dir_it(p); dir_it != directory_iterator(); ++dir_it) {
                const path& file_path = dir_it->path();
                if (!is_regular_file(file_path))
                    continue;
                if (file_path.extension() != SP_SAVE_FILE_EXTENSION &&
                    file_path.extension() != MP_SAVE_FILE_EXTENSION)
                { continue; }

                std::time_t t = last_write_time(file_path);
                files_by_write_time.insert({t, file_path});
            }

            //DebugLogger() << "files by write time:";
            //for (std::multimap<std::time_t, path>::value_type& entry : files_by_write_time)
            //{ DebugLogger() << entry.first << " : " << entry.second.filename(); }

            int num_to_delete = files_by_write_time.size() - files_limit + 1;   // +1 because will add a new file after deleting, bringing number back up to limit
            if (num_to_delete <= 0)
                return; // don't need to delete anything.

            int num_deleted = 0;
            for (std::multimap<std::time_t, path>::value_type& entry : files_by_write_time) {
                if (num_deleted >= num_to_delete)
                    break;
                remove(entry.second);
                ++num_deleted;
            }
        } catch (...) {
            ErrorLogger() << "Error removing oldest files";
        }
    }

    boost::filesystem::path CreateNewAutosaveFilePath(int client_empire_id, bool is_single_player) {
        const char* legal_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_-";

        // get empire name, filtered for filename acceptability
        const Empire* empire = GetEmpire(client_empire_id);
        std::string empire_name;
        if (empire)
            empire_name = empire->Name();
        else
            empire_name = UserString("OBSERVER");
        std::string::size_type first_good_empire_char = empire_name.find_first_of(legal_chars);
        if (first_good_empire_char == std::string::npos) {
            empire_name.clear();
        } else {
            std::string::size_type first_bad_empire_char = empire_name.find_first_not_of(legal_chars, first_good_empire_char);
            empire_name = empire_name.substr(first_good_empire_char, first_bad_empire_char - first_good_empire_char);
        }

        // get player name, also filtered
        std::string player_name;
        if (empire)
            player_name = empire->PlayerName();
        std::string::size_type first_good_player_char = player_name.find_first_of(legal_chars);
        if (first_good_player_char == std::string::npos) {
            player_name.clear();
        } else {
            std::string::size_type first_bad_player_char = player_name.find_first_not_of(legal_chars, first_good_player_char);
            player_name = player_name.substr(first_good_player_char, first_bad_player_char - first_good_player_char);
        }

        // select filename extension
        const auto& extension = is_single_player ? SP_SAVE_FILE_EXTENSION : MP_SAVE_FILE_EXTENSION;

        // Add timestamp to autosave generated files
        std::string datetime_str = FilenameTimestamp();

        boost::filesystem::path autosave_dir_path(GetSaveDir() / "auto");

        std::string save_filename = boost::io::str(boost::format("FreeOrion_%s_%s_%04d_%s%s") % player_name % empire_name % CurrentTurn() % datetime_str % extension);
        boost::filesystem::path save_path(autosave_dir_path / save_filename);

        try {
            // ensure autosave directory exists
            if (!exists(autosave_dir_path))
                boost::filesystem::create_directories(autosave_dir_path);
        } catch (const std::exception& e) {
            ErrorLogger() << "Autosave unable to check / create autosave directory: " << e.what();
        }

        return save_path;
    }
}

void HumanClientApp::Autosave() {
    // Create an auto save for 1) new games on turn 1, 2) if auto save is
    // requested on turn number modulo autosave.turns or 3) on the last turn of
    // play.

    // autosave only on appropriate turn numbers, and when enabled for current
    // game type (single vs. multiplayer)
    int autosave_turns = GetOptionsDB().Get<int>("autosave.turns");
    bool is_valid_autosave =
        (autosave_turns > 0
         && CurrentTurn() % autosave_turns == 0
         && ((m_single_player_game && GetOptionsDB().Get<bool>("autosave.single-player"))
             || (!m_single_player_game && GetOptionsDB().Get<bool>("autosave.multiplayer"))));

    // is_initial_save is gated in HumanClientFSM for new game vs loaded game
    bool is_initial_save = (GetOptionsDB().Get<bool>("autosave.initial-turn") && CurrentTurn() == 1);
    bool is_final_save = (GetOptionsDB().Get<bool>("autosave.last-turn") && !m_game_started);

    if (!(is_initial_save || is_valid_autosave || is_final_save))
        return;

    auto autosave_file_path = CreateNewAutosaveFilePath(EmpireID(), m_single_player_game);

    // check for and remove excess oldest autosaves
    boost::filesystem::path autosave_dir_path(GetSaveDir() / "auto");
    int max_autosaves = GetOptionsDB().Get<int>("autosave.limit");
    RemoveOldestFiles(max_autosaves, autosave_dir_path);

    // create new save
    auto path_string = PathString(autosave_file_path);

    if (is_initial_save)
        DebugLogger() << "Turn 0 autosave to: " << path_string;
    if (is_valid_autosave)
        DebugLogger() << "Autosave to: " << path_string;
    if (is_final_save)
        DebugLogger() << "End of play autosave to: " << path_string;

    try {
        SaveGame(path_string);
    } catch (const std::exception& e) {
        ErrorLogger() << "Autosave failed: " << e.what();
    }
}

std::string HumanClientApp::SelectLoadFile() {
    auto sfd = GG::Wnd::Create<SaveFileDialog>(true);
    sfd->Run();
    return sfd->Result();
}

void HumanClientApp::ResetClientData() {
    m_networking->SetPlayerID(Networking::INVALID_PLAYER_ID);
    m_networking->SetHostPlayerID(Networking::INVALID_PLAYER_ID);
    SetEmpireID(ALL_EMPIRES);
    m_ui->GetMapWnd()->Sanitize();

    m_universe.Clear();
    m_empires.Clear();
    m_orders.Reset();
    GetCombatLogManager().Clear();
}

namespace {
    // Ask the player if they want to wait for the save game to complete
    // The dialog automatically closes if the save completes while the user is waiting
    class SaveGamePendingDialog : public GG::ThreeButtonDlg {
        public:
        SaveGamePendingDialog(bool reset, boost::signals2::signal<void()>& save_completed_signal) :
            GG::ThreeButtonDlg(
                GG::X(320), GG::Y(200), UserString("SAVE_GAME_IN_PROGRESS"),
                ClientUI::GetFont(ClientUI::Pts()+2),
                ClientUI::WndColor(), ClientUI::WndOuterBorderColor(),
                ClientUI::CtrlColor(), ClientUI::TextColor(), 1,
                (reset ? UserString("ABORT_SAVE_AND_RESET") : UserString("ABORT_SAVE_AND_EXIT")))
        {
            save_completed_signal.connect(
                boost::bind(&SaveGamePendingDialog::SaveCompletedHandler, this));
        }

        void SaveCompletedHandler() {
            DebugLogger() << "SaveGamePendingDialog::SaveCompletedHandler save game completed handled.";
            m_done = true;
        }
    };
}

void HumanClientApp::ResetToIntro(bool skip_savegame)
{ ResetOrExitApp(true, skip_savegame); }

void HumanClientApp::ExitApp()
{ ResetOrExitApp(false, false); }

void HumanClientApp::ResetOrExitApp(bool reset, bool skip_savegame) {
    DebugLogger() << (reset ? "HumanClientApp::ResetToIntro" : "HumanClientApp::ExitApp");

    auto was_playing = m_game_started;
    m_game_started = false;

    // Only save if not exiting due to an error.
    if (!skip_savegame) {
        if (was_playing && GetOptionsDB().Get<bool>("autosave.last-turn"))
            Autosave();

        if (!m_game_saves_in_progress.empty()) {
            DebugLogger() << "save game in progress. Checking with player.";
            auto dlg = GG::Wnd::Create<SaveGamePendingDialog>(reset, this->SaveGamesCompletedSignal);
            dlg->Run();
        }
    }

    m_fsm->process_event(StartQuittingGame(reset, m_server_process));
}

void HumanClientApp::InitAutoTurns(int auto_turns) {
    if (auto_turns > 0)
        m_auto_turns = auto_turns;
    else
        m_auto_turns = 0;
}

void HumanClientApp::DecAutoTurns(int n) {
    m_auto_turns -= n;
    if (m_auto_turns < 0)
        m_auto_turns = 0;
}

int HumanClientApp::AutoTurnsLeft() const
{ return m_auto_turns; }

bool HumanClientApp::HaveWindowFocus() const
{ return m_have_window_focus; }

int HumanClientApp::EffectsProcessingThreads() const
{ return GetOptionsDB().Get<int>("effects-threads-ui"); }

void HumanClientApp::UpdateFPSLimit() {
    if (GetOptionsDB().Get<bool>("limit-fps")) {
        double fps = GetOptionsDB().Get<double>("max-fps");
        SetMaxFPS(fps);
        DebugLogger() << "Limited FPS to " << fps;
    } else {
        SetMaxFPS(0.0); // disable fps limit
        DebugLogger() << "Disabled FPS limit";
    }
}

void HumanClientApp::DisconnectedFromServer() {
    DebugLogger() << "HumanClientApp::DisconnectedFromServer";
    m_fsm->process_event(Disconnection());
}

HumanClientApp* HumanClientApp::GetApp()
{ return dynamic_cast<HumanClientApp*>(GG::GUI::GetGUI()); }

void HumanClientApp::Initialize()
{}

void HumanClientApp::OpenURL(const std::string& url) {
    // make sure it's a legit url
    std::string trimmed_url = url;
    boost::algorithm::trim(trimmed_url);
    // shouldn't be excessively long
    if (trimmed_url.size() > 500) { // arbitrary limit
        ErrorLogger() << "HumanClientApp::OpenURL given bad-looking url (too long): " << trimmed_url;
        return;
    }
    // should start with http:// or https://
    if (trimmed_url.size() < 8) {
        ErrorLogger() << "HumanClientApp::OpenURL given bad-looking url (too short): " << trimmed_url;
        return;
    }
    if (trimmed_url.find_first_of("http://") != 0 &&
        trimmed_url.find_first_of("https://") != 0)
    {
        ErrorLogger() << "HumanClientApp::OpenURL given url that doesn't start with http:// :" << trimmed_url;
        return;
    }
    // should not have newlines...
    if (trimmed_url.find_first_of("\n") != std::string::npos) {
        ErrorLogger() << "HumanClientApp::OpenURL given url that contains a newline. rejecting.";
        return;
    }

    // append url to OS-specific open command
    std::string command;
#ifdef _WIN32
    command += "start ";
#elif __APPLE__
    command += "open ";
#else
    command += "xdg-open ";
#endif
    command += url;

    // execute open command
    system(command.c_str());
}
