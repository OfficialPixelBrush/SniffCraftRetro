#include "sniffcraft/conf.hpp"
#include "sniffcraft/Logger.hpp"
#include "sniffcraft/MinecraftProxy.hpp"
#include "sniffcraft/Protocol.hpp"
#include "sniffcraft/server.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <algorithm>
#include <iostream>
#include <utility>

#ifdef WITH_GUI
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

constexpr int base_window_height = 960;
constexpr int base_window_width = 1200;
#endif

Server::Server()
{
    std::shared_lock<std::shared_mutex> conf_lock(Conf::conf_mutex);
    const Json::Value conf = Conf::LoadConf();
    client_port = conf[Conf::local_port_key].get_number<unsigned short>();
    server_address = conf[Conf::server_address_key].get_string();
    ResolveIpPortFromAddress();

    proxies_cleaning_thread = std::thread(&Server::CleanProxies, this);
}

Server::~Server()
{
#ifdef WITH_GUI
    io_context.stop();
    if (iocontext_thread.joinable())
    {
        iocontext_thread.join();
    }
#endif

    if (proxies_cleaning_thread_running)
    {
        proxies_cleaning_thread_running = false;
        proxies_cleaning_thread.join();
    }
}

void Server::run()
{
#ifdef WITH_GUI
    if (Conf::headless)
    {
        run_iocontext();
    }
    else
    {
        Render();
    }
#else
    run_iocontext();
#endif
}

void Server::run_iocontext()
{
    acceptor = std::make_unique<asio::ip::tcp::acceptor>(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), client_port));
    listen_connection();
    std::cout << "Starting redirection of any connection on 127.0.0.1:" << client_port << " to " << server_ip << ":" << server_port << std::endl;
    io_context.run();
}

void Server::listen_connection()
{
    BaseProxy* proxy = GetNewMinecraftProxy();

    acceptor->async_accept(proxy->ClientSocket(),
        std::bind(&Server::handle_accept, this, proxy,
            std::placeholders::_1));
}

void Server::handle_accept(BaseProxy* new_proxy, const asio::error_code& ec)
{
    if (!ec)
    {
        const std::optional<std::string> proxy_start_return = new_proxy->Start(server_ip, server_port);
        if (proxy_start_return.has_value())
        {
            std::cerr << "Failed to start new proxy: " << proxy_start_return.value() << std::endl;
        }
#ifdef WITH_GUI
        {
            std::scoped_lock<std::mutex> lock(loggers_mutex);
            connection_error = proxy_start_return;
            if (!connection_error.has_value())
            {
                if (MinecraftProxy* casted_proxy = dynamic_cast<MinecraftProxy*>(new_proxy))
                {
                    loggers.push_back(casted_proxy->GetLogger());
                }
            }
        }
#endif
    }
    else
    {
        std::cerr << "Failed to start new proxy" << std::endl;
    }
    listen_connection();
}

void Server::ResolveIpPortFromAddress()
{
    // Beta 1.7.3 clients don't do any SRV DNS lookup, the address is always host[:port]
    server_port = 25565;
    server_ip = server_address;

    // Only one ':' means "host:port", more means a raw IPv6 address (or [IPv6]:port)
    const size_t last_colon = server_address.rfind(':');
    if (last_colon == std::string::npos)
    {
        return;
    }
    const bool bracketed_ipv6 = server_address.find(']') != std::string::npos;
    if (!bracketed_ipv6 && server_address.find(':') != last_colon)
    {
        return;
    }
    try
    {
        const int port = std::stoi(server_address.substr(last_colon + 1));
        if (port > 0 && port < 65536)
        {
            server_port = static_cast<unsigned short>(port);
            server_ip = server_address.substr(0, last_colon);
            if (bracketed_ipv6 && server_ip.size() > 2 && server_ip.front() == '[' && server_ip.back() == ']')
            {
                server_ip = server_ip.substr(1, server_ip.size() - 2);
            }
        }
    }
    catch (const std::exception&)
    {
        // Not a port, the whole string is the host
    }
}

BaseProxy* Server::GetNewMinecraftProxy()
{
    std::lock_guard<std::mutex> lock(proxies_mutex);
    // Create a new proxy
    std::unique_ptr<BaseProxy> proxy = std::make_unique<MinecraftProxy>(io_context);
    proxies.push_back(std::move(proxy));

    return proxies.back().get();
}

void Server::CleanProxies()
{
    proxies_cleaning_thread_running = true;
    while (proxies_cleaning_thread_running)
    {
        {
            std::lock_guard<std::mutex> lock(proxies_mutex);
            // Clean old proxies
            for (int i = static_cast<int>(proxies.size()) - 1; i > -1; --i)
            {
                if (proxies[i]->Started() && !proxies[i]->Running())
                {
                    proxies.erase(proxies.begin() + i);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

#ifdef WITH_GUI
void Server::Render()
{
    if (!glfwInit())
    {
        const char* description;
        int code = glfwGetError(&description);
        std::cerr << "Failed to initialize GLFW (error " << code << ": \"" << description << "\")" << ", you can launch SniffCraft without GUI with the --headless argument" << std::endl;
        return;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
    GLFWwindow* window = glfwCreateWindow(base_window_width, base_window_height, "SniffCraft", NULL, NULL);
    if (window == NULL)
    {
        const char* description;
        int code = glfwGetError(&description);
        std::cerr << "Failed to create GLFW window (error " << code << ": \"" << description << "\")" << ", you can launch SniffCraft without GUI with the --headless argument" << std::endl;
        glfwTerminate();
        return;
    }

    glfwSetWindowUserPointer(window, this);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths)
        {
            Server* server = static_cast<Server*>(glfwGetWindowUserPointer(w));
            std::scoped_lock lock(server->loggers_mutex);
            for (int i = 0; i < count; ++i)
            {
                try
                {
                    server->loggers.push_back(std::make_shared<Logger>(std::filesystem::path(paths[i])));
                }
                catch (std::runtime_error& e)
                {
                    std::cerr << "Error trying to load binary file: " << e.what() << std::endl;
                }
            }
        });

    // imgui: setup context
    // ---------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Style
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = NULL;
    ImGui::GetIO().LogFilename = NULL;

    // Setup platform/renderer
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    const float main_scale = ImGui_ImplGlfw_GetContentScaleForWindow(window);
    glfwSetWindowSize(window, base_window_width * main_scale, base_window_height * main_scale);

    ImGui::GetStyle().ScaleAllSizes(main_scale);
    ImGui::GetStyle().FontScaleDpi = main_scale;

    InternalRenderLoop(window);

    // ImGui cleaning
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

void HelpMarker(const char* tooltip);

void Server::InternalRenderLoop(GLFWwindow* window)
{
    // A bunch of variables used as display data during one frame
    // App parameters
    bool started = false;
    std::string local_port_str = std::to_string(client_port);
    std::string active_conf = "";
    std::set<std::string> conf_list = Conf::GetConfList();
    bool log_to_console = false;
    bool network_recap_to_console = false;
    bool log_to_text_file = false;
    bool log_to_bin_file = false;
    bool log_raw_bytes = false;
    float main_scale = ImGui::GetStyle()._MainScale;
    // Used to reset all the values before rescaling
    const ImGuiStyle style_copy = ImGui::GetStyle();

    // Display filter
    const std::vector<std::string> direction_str = {
        "Server --> Client",
        "Client --> Server"
    };
    int selected_direction = 0;
    std::vector<Protocol::PacketInfo> displayed;
    std::vector<Protocol::PacketInfo> hidden;

    const auto on_conf_changed = [&]()
    {
        std::shared_lock<std::shared_mutex> lock(Conf::conf_mutex);
        const Json::Value conf = Conf::LoadConf();

        active_conf = Conf::active_conf.value();
        log_to_console = conf.contains(Conf::console_log_key) && conf[Conf::console_log_key].is_bool() && conf[Conf::console_log_key].get<bool>();
        network_recap_to_console = conf.contains(Conf::network_recap_to_console_key) && conf[Conf::network_recap_to_console_key].is_bool() && conf[Conf::network_recap_to_console_key].get<bool>();
        log_to_text_file = conf.contains(Conf::text_file_log_key) && conf[Conf::text_file_log_key].is_bool() && conf[Conf::text_file_log_key].get<bool>();
        log_to_bin_file = conf.contains(Conf::binary_file_log_key) && conf[Conf::binary_file_log_key].is_bool() && conf[Conf::binary_file_log_key].get<bool>();
        log_raw_bytes = conf.contains(Conf::raw_bytes_log_key) && conf[Conf::raw_bytes_log_key].is_bool() && conf[Conf::raw_bytes_log_key].get<bool>();

        displayed.clear();
        hidden.clear();
        // Packets ignored in the currently selected direction
        Json::Value ignored_list = Json::Array();
        const std::string& ignored_key = selected_direction == 0 ? Conf::ignored_clientbound_key : Conf::ignored_serverbound_key;
        if (conf.contains(Conf::packets_key) && conf[Conf::packets_key].contains(ignored_key) && conf[Conf::packets_key][ignored_key].is_array())
        {
            ignored_list = conf[Conf::packets_key][ignored_key];
        }
        for (const auto& s : Protocol::AllPackets())
        {
            bool ignored = false;
            for (const auto& v : ignored_list.get_array())
            {
                if ((v.is_number() && v.get_number<int>() == s.id) ||
                    (v.is_string() && v.get_string() == s.name))
                {
                    ignored = true;
                    break;
                }
            }
            (ignored ? hidden : displayed).push_back(s);
        }

        std::sort(displayed.begin(), displayed.end(), [](const Protocol::PacketInfo& a, const Protocol::PacketInfo& b) { return a.name < b.name; });
        std::sort(hidden.begin(), hidden.end(), [](const Protocol::PacketInfo& a, const Protocol::PacketInfo& b) { return a.name < b.name; });

        {
            // Triger a load config for all associated loggers
            std::scoped_lock<std::mutex> lock(loggers_mutex);
            for (auto& l : loggers)
            {
                l->LoadConfig();
            }
        }
    };
    const auto on_ignored_changed = [&]()
    {
        std::sort(displayed.begin(), displayed.end(), [](const Protocol::PacketInfo& a, const Protocol::PacketInfo& b) { return a.name < b.name; });
        std::sort(hidden.begin(), hidden.end(), [](const Protocol::PacketInfo& a, const Protocol::PacketInfo& b) { return a.name < b.name; });

        {
            std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
            Json::Value conf = Conf::LoadConf();
            std::vector<std::string_view> ignored_names(hidden.size());
            for (size_t i = 0; i < hidden.size(); ++i)
            {
                ignored_names[i] = hidden[i].name;
            }
            conf[Conf::packets_key][selected_direction == 0 ? Conf::ignored_clientbound_key : Conf::ignored_serverbound_key] = ignored_names;
            Conf::SaveConf(conf);
        }
        {
            // Triger a load config for all associated loggers
            std::scoped_lock<std::mutex> lock(loggers_mutex);
            for (auto& l : loggers)
            {
                l->LoadConfig();
            }
        }
    };

    on_conf_changed();

    while (glfwWindowShouldClose(window) == 0)
    {
        // clear the window
        glClear(GL_COLOR_BUFFER_BIT);

        // Init imgui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

            ImGui::Begin("SniffCraft Main Window", NULL,
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse
            );
            ImGui::SeparatorText("Application parameters");
            {
                ImGui::TextUnformatted("App scale: ");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::CalcTextSize("0.000").x);
                if (ImGui::InputFloat("##scale_input", &main_scale, 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_::ImGuiInputTextFlags_CharsDecimal))
                {
                    main_scale = std::clamp(main_scale, 0.25f, 4.0f);
                    ImGui::GetStyle() = style_copy;
                    ImGui::GetStyle().ScaleAllSizes(main_scale);
                    ImGui::GetStyle().FontScaleDpi = main_scale;
                    glfwSetWindowSize(window, base_window_width * main_scale, base_window_height * main_scale);
                }
                ImGui::SameLine();
                bool conf_changed = false;
                {
                    std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
                    ImGui::TextUnformatted("Conf name");
                    ImGui::SameLine();
                    HelpMarker("Change the name to save the current configuration under a new name");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(0.7f * ImGui::GetContentRegionAvail().x);
                    ImGui::InputText("##conf_name", &active_conf);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        const Json::Value conf = Conf::LoadConf();
                        Conf::active_conf = active_conf;
                        conf_list.insert(active_conf);
                        Conf::SaveConf(conf);
                        ResolveIpPortFromAddress();
                    }
                    ImGui::SameLine();
                    if (ImGui::BeginCombo("##conf_list", active_conf.c_str(), ImGuiComboFlags_WidthFitPreview))
                    {
                        bool deleted = false;
                        for (const auto& s : conf_list)
                        {
                            ImGui::BeginDisabled(s == active_conf);
                            ImGui::PushID(s.c_str());
                            if (ImGui::Button("X"))
                            {
                                Conf::DeleteConf(s);
                                deleted = true;
                            }
                            ImGui::PopID();
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s", "Delete this configuration");
                            }
                            ImGui::SameLine();
                            if (ImGui::Selectable(s.c_str()))
                            {
                                Conf::active_conf = s;
                                const Json::Value conf = Conf::LoadConf();
                                Conf::SaveConf(conf);
                                conf_changed = true;
                            }
                            ImGui::EndDisabled();
                        }
                        if (deleted)
                        {
                            conf_list = Conf::GetConfList();
                        }
                        ImGui::EndCombo();
                    }
                }
                if (conf_changed)
                {
                    on_conf_changed();
                }
                ImGui::TextUnformatted("Server address");
                ImGui::SameLine();
                HelpMarker("Server address, as you would enter it in a minecraft client");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(0.7f * ImGui::GetContentRegionAvail().x);
                ImGui::InputText("##server_address", &server_address);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
                    Json::Value conf = Conf::LoadConf();
                    conf[Conf::server_address_key] = server_address;
                    Conf::SaveConf(conf);
                    ResolveIpPortFromAddress();
                }

                ImGui::BeginDisabled(started);
                float current_x = ImGui::GetItemRectMax().x;
                // Stay on this line only if it fits
                if (
                    current_x +
                    ImGui::CalcTextSize("Local port").x +
                    ImGui::CalcTextSize("25555").x +
                    ImGui::CalcTextSize("(?)").x +
                    3 * ImGui::GetStyle().ItemSpacing.x < ImGui::GetContentRegionAvail().x
                )
                {
                    ImGui::SameLine(0.0f, 10.0f);
                }
                ImGui::TextUnformatted("Local port");
                ImGui::SameLine();
                HelpMarker("Local port clients will use to connect to this Sniffcraft instance, should be between 1024 and 65535");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                ImGui::InputText("##local_port", &local_port_str, ImGuiInputTextFlags_::ImGuiInputTextFlags_CharsDecimal);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    try
                    {
                        client_port = static_cast<unsigned short>(std::stoul(local_port_str));
                    }
                    catch (std::exception&)
                    {
                        local_port_str = std::to_string(client_port);
                    }
                    std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
                    Json::Value conf = Conf::LoadConf();
                    conf[Conf::local_port_key] = client_port;
                    Conf::SaveConf(conf);
                }
                ImGui::EndDisabled();

                if (ImGui::Checkbox("Console", &log_to_console))
                {
                    std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
                    Json::Value conf = Conf::LoadConf();
                    conf[Conf::console_log_key] = log_to_console;
                    Conf::SaveConf(conf);
                }
                ImGui::SameLine();
                HelpMarker("If checked, logs will be printed in the console");
                current_x = ImGui::GetItemRectMax().x;
                // Stay on this line only if it fits
                if (
                    current_x +
                    ImGui::CalcTextSize("Network recap").x +
                    ImGui::CalcTextSize("(?)").x +
                    ImGui::GetFrameHeight() +
                    2 * ImGui::GetStyle().ItemSpacing.x < ImGui::GetContentRegionAvail().x
                    )
                {
                    ImGui::SameLine(0.0f, 10.0f);
                }
                if (ImGui::Checkbox("Network recap", &network_recap_to_console))
                {
                    std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
                    Json::Value conf = Conf::LoadConf();
                    conf[Conf::network_recap_to_console_key] = network_recap_to_console;
                    Conf::SaveConf(conf);
                }
                ImGui::SameLine();
                HelpMarker("If checked, a network usage recap will be printed in the console every ~10 seconds");
                current_x = ImGui::GetItemRectMax().x;
                // Stay on this line only if it fits
                if (
                    current_x +
                    ImGui::CalcTextSize("Raw bytes").x +
                    ImGui::CalcTextSize("(?)").x +
                    ImGui::GetFrameHeight() +
                    2 * ImGui::GetStyle().ItemSpacing.x < ImGui::GetContentRegionAvail().x
                    )
                {
                    ImGui::SameLine(0.0f, 10.0f);
                }
                if (ImGui::Checkbox("Raw bytes", &log_raw_bytes))
                {
                    std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
                    Json::Value conf = Conf::LoadConf();
                    conf[Conf::raw_bytes_log_key] = log_raw_bytes;
                    Conf::SaveConf(conf);
                }
                ImGui::SameLine();
                HelpMarker("If checked, raw bytes will be added in the console and text file");
                current_x = ImGui::GetItemRectMax().x;
                // Stay on this line only if it fits
                if (
                    current_x +
                    ImGui::CalcTextSize("Text file").x +
                    ImGui::CalcTextSize("(?)").x +
                    ImGui::GetFrameHeight() +
                    2 * ImGui::GetStyle().ItemSpacing.x < ImGui::GetContentRegionAvail().x
                    )
                {
                    ImGui::SameLine(0.0f, 10.0f);
                }
                if (ImGui::Checkbox("Text file", &log_to_text_file))
                {
                    std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
                    Json::Value conf = Conf::LoadConf();
                    conf[Conf::text_file_log_key] = log_to_text_file;
                    Conf::SaveConf(conf);
                }
                ImGui::SameLine();
                HelpMarker("If checked, logs will be sent to a text file");
                current_x = ImGui::GetItemRectMax().x;
                // Stay on this line only if it fits
                if (
                    current_x +
                    ImGui::CalcTextSize("Binary file").x +
                    ImGui::CalcTextSize("(?)").x +
                    ImGui::GetFrameHeight() +
                    2 * ImGui::GetStyle().ItemSpacing.x < ImGui::GetContentRegionAvail().x
                    )
                {
                    ImGui::SameLine(0.0f, 10.0f);
                }
                if (ImGui::Checkbox("Binary file", &log_to_bin_file))
                {
                    std::scoped_lock<std::shared_mutex> lock(Conf::conf_mutex);
                    Json::Value conf = Conf::LoadConf();
                    conf[Conf::binary_file_log_key] = log_to_bin_file;
                    Conf::SaveConf(conf);
                }
                ImGui::SameLine();
                HelpMarker("If checked, packets will be saved in a binary file that can be reimported into Sniffcraft GUI");

                if (!started)
                {
                    ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                    if (ImGui::Button("Start"))
                    {
                        started = true;
                        iocontext_thread = std::thread(&Server::run_iocontext, this);
                    }
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::Text("Connect your client to 127.0.0.1:%i and it will be redirected to %s:%i", client_port, server_ip.c_str(), server_port);
                    {
                        std::scoped_lock<std::mutex> lock(loggers_mutex);
                        if (connection_error.has_value())
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                            ImGui::TextUnformatted(connection_error.value().c_str());
                            ImGui::PopStyleColor();
                        }
                    }
                }
            }


            ImGui::SeparatorText("Display filter");
            {
                if (ImGui::BeginCombo("##network_direction", direction_str[selected_direction].c_str(), ImGuiComboFlags_WidthFitPreview))
                {
                    for (size_t n = 0; n < direction_str.size(); ++n)
                    {
                        const bool is_selected = selected_direction == n;
                        if (ImGui::Selectable(direction_str[n].c_str(), is_selected))
                        {
                            selected_direction = static_cast<int>(n);
                            if (!is_selected)
                            {
                                on_conf_changed();
                            }
                        }
                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Hide all"))
                {
                    hidden.insert(hidden.end(), displayed.begin(), displayed.end());
                    displayed.clear();
                    on_ignored_changed();
                }
                ImGui::SameLine();
                if (ImGui::Button("Show all"))
                {
                    displayed.insert(displayed.end(), hidden.begin(), hidden.end());
                    hidden.clear();
                    on_ignored_changed();
                }

                const float half_size = 0.5f * (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x);
                ImGui::TextUnformatted("Displayed packets (double click to ignore)");
                const float displayed_size = ImGui::GetItemRectSize().x;
                ImGui::SameLine();
                ImGui::Dummy(ImVec2(half_size - displayed_size - ImGui::GetStyle().ItemSpacing.x, ImGui::GetTextLineHeightWithSpacing()));
                ImGui::SameLine();
                ImGui::TextUnformatted("Hidden packets");
                if (ImGui::BeginListBox("##displayed_packets", ImVec2(half_size, 7 * ImGui::GetTextLineHeightWithSpacing())))
                {
                    for (size_t i = 0; i < displayed.size(); ++i)
                    {
                        ImGui::Selectable(displayed[i].name.data(), false, ImGuiSelectableFlags_AllowDoubleClick);
                        if (ImGui::IsItemHovered())
                        {
                            if (ImGui::BeginTooltip())
                            {
                                ImGui::Text("ID: %i", displayed[i].id);
                                ImGui::EndTooltip();
                            }
                            if (ImGui::IsMouseDoubleClicked(0))
                            {
                                hidden.push_back(displayed[i]);
                                displayed.erase(displayed.begin() + i);
                                --i;
                                on_ignored_changed();
                            }
                        }
                    }
                    ImGui::EndListBox();
                }
                ImGui::SameLine();
                if (ImGui::BeginListBox("##hidden_packets", ImVec2(half_size, 7 * ImGui::GetTextLineHeightWithSpacing())))
                {
                    for (size_t i = 0; i < hidden.size(); ++i)
                    {
                        ImGui::Selectable(hidden[i].name.data(), false, ImGuiSelectableFlags_AllowDoubleClick);
                        if (ImGui::IsItemHovered())
                        {
                            if (ImGui::BeginTooltip())
                            {
                                ImGui::Text("ID: %i", hidden[i].id);
                                ImGui::EndTooltip();
                            }
                            if (ImGui::IsMouseDoubleClicked(0))
                            {
                                displayed.push_back(hidden[i]);
                                hidden.erase(hidden.begin() + i);
                                --i;
                                on_ignored_changed();
                            }
                        }
                    }
                    ImGui::EndListBox();
                }
            }

            ImGui::SeparatorText("Sessions");
            std::pair<int, Endpoint> additional_ignored_packet = { -1, Endpoint::Client };
            {
                std::scoped_lock<std::mutex> lock(loggers_mutex);
                if (loggers.size() > 0 && ImGui::BeginTabBar("loggers", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
                {
                    int index = 0;
                    for (auto it = loggers.begin(); it != loggers.end(); )
                    {
                        ImGui::PushID(index);
                        bool open = true;
                        if (ImGui::BeginTabItem((*it)->GetBaseFilename().c_str(), &open, ImGuiTabItemFlags_None))
                        {
                            // If a packet ignore button has been pressed, save it to update display later
                            auto ignored_pressed = (*it)->Render();
                            if (ignored_pressed.first != -1)
                            {
                                additional_ignored_packet = ignored_pressed;
                            }
                            ImGui::EndTabItem();
                        }
                        if (!open)
                        {
                            it = loggers.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                        index += 1;
                        ImGui::PopID();
                    }
                    ImGui::EndTabBar();
                }
            }

            // Update display afterward because the loggers are locked while rendered
            const auto& [ignored_id, origin] = additional_ignored_packet;
            if (ignored_id != -1)
            {
                const int current_selected_direction = selected_direction;
                // Switch display to the one matching the packet
                selected_direction = static_cast<int>(origin);
                on_conf_changed();
                // Add this packet to hidden
                hidden.push_back(Protocol::PacketInfo{ ignored_id, Protocol::PacketName(ignored_id) });
                for (int i = static_cast<int>(displayed.size()) - 1; i > -1; --i)
                {
                    if (displayed[i].id == ignored_id)
                    {
                        displayed.erase(displayed.begin() + i);
                    }
                }
                on_ignored_changed();
                // Switch back to the previously selected display
                selected_direction = current_selected_direction;
                on_conf_changed();
            }
            ImGui::End();
        }

        // Render ImGui
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // swap buffer
        glfwSwapBuffers(window);

        // process user events
        glfwPollEvents();
    }
}

void HelpMarker(const char* tooltip)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(tooltip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
#endif
