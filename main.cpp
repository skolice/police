#include "libs/alma/alma.hpp"
#include "libs/rvmt/rvmt.hpp"
#include "libs/pcg-cpp/pcg_random.hpp"
#include <X11/Xatom.h>


#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <random>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

Display* rootDisplay;
Window rootWindow;

enum clientType {
    clientType_UNKNOWN,
    clientType_FORGE1, // Forge 1.7.10
    clientType_FORGE2, // Forge 1.8.9
    clientType_LUNAR1, // Lunar 1.7.10
    clientType_LUNAR2 // Lunar 1.8.9
};

bool nineSizedAddresses = false;

clientType clientType_CURRENT = clientType_UNKNOWN;
std::string clientType_TITLE;

int __NULLINT;
unsigned int __NULLUINT;
unsigned long __NULLULONG;

bool leftEnabled = false;
bool leftContainerClicks = false;
bool leftAllowedSlots[9] = {true, true, true, true, true, true, true, true, true};
float lCPS = 10.0;

bool rightEnabled = false;
bool rightContainerClicks = false;
bool rightAllowedSlots[9] = {true, true, true, true, true, true, true, true, true};
float rightDelay = 250;

bool isPlayerSprinting = false;
bool isInContainer = false;
bool isGamePaused = false;

unsigned char activeSlot = 0;

std::atomic<bool> destructing = false;
std::atomic<bool> leftThreadDone = false;
std::atomic<bool> rightThreadDone = false;
std::atomic<bool> playerPtrThreadDone = false;

void leftThreadFunc();
void rightThreadFunc();
void playerPointerThreadFunc();

float random_float(float range_min, float range_max);
int random_int(int range_min, int range_max);
int randomizer(float cps);

void mouseButtonInstruction(int mouseButton, int instruction);
bool isMouseButtonHeld(unsigned int mouseButton);
bool isHotbarEnabled(bool* var);
bool isActiveWindowMinecraft();

enum GUIPages {
    GUIPages_COMBAT,
    GUIPages_MISC,
    GUIPages_SETTINGS,
};
GUIPages GUIPages_CURRENT = GUIPages_COMBAT;

int main(int argc, char** argv) {
    // Start X11 Resources.
    rootDisplay = XOpenDisplay(NULL);
    rootWindow = XDefaultRootWindow(rootDisplay);

    // === Check for root privileges.
    std::ifstream file("/proc/self/status", std::ios::binary | std::ios::in);
    std::stringstream sstream;
    sstream << file.rdbuf();

    // From what i've read, this condition will work on most distros.
    if (sstream.str().find("Uid:\t0\t0\t0\t0") == std::string::npos) {
        std::cout << "No root privileges. Remember to switch to the root user.";
        return 1; // No root privileges
    }

    // === Automatically detect Minecraft's PID
    unsigned int clientPID = 0;
    for (const auto& folder : std::filesystem::directory_iterator(std::filesystem::path("/proc"))) 
    if (std::filesystem::is_directory(folder)) {

        // Dump comm contents into a string to find the version.
        std::ifstream ifstream(folder.path() / "comm");
        std::string string;
        std::getline(ifstream, string);
        ifstream.close();

        // Check if it's a java process
        if (string == "java") {
            ifstream.open(folder.path() / "cmdline");
            std::getline(ifstream, string);
            ifstream.close();

            // Check for Lunar
            if (string.find(",lunar.") != std::string::npos) {
                // 1.7.10
                if (string.find("OptiFine_v1_7.jar") != std::string::npos) {
                    clientType_CURRENT = clientType_LUNAR1;
                    clientType_TITLE = "Client 1.7.10";
                    clientPID = std::stoi(folder.path().filename().string());
                    break;
                }
                // 1.8.9
                if (string.find("OptiFine_v1_8.jar") != std::string::npos) {
                    clientType_CURRENT = clientType_LUNAR2;
                    clientType_TITLE = "Client 1.8.9";
                    clientPID = std::stoi(folder.path().filename().string());
                    break;
                }
            }

            // Check for Forge 1.7.10.
            if (string.find("minecraftforge/forge/1.7.10") != std::string::npos) {
                clientType_CURRENT = clientType_FORGE1;
                clientType_TITLE = "Minecraft 1.7.10";
                clientPID = std::stoi(folder.path().filename().string());
                break;
            }

            // Check for Forge 1.8.9.
            if (string.find("minecraftforge/forge/1.8.9") != std::string::npos) {
                clientType_CURRENT = clientType_FORGE2;
                clientType_TITLE = "Minecraft 1.8.9";
                clientPID = std::stoi(folder.path().filename().string());
                break;
            }

        }
    }

    if (clientPID == 0) { // Minecraft couldn't be detected automatically.
        std::cout << "Couldn't find a compatible Minecraft version.";
        return 1;
    }

    if (!alma::openProcess(clientPID)) { // Can't access the pid's memory file
        std::cout << "Error while opening Minecraft's memory file.";
        return 1;
    }

    // === Check addresses length.
    std::vector<memoryPage> _STARTUPPROCESSPAGES = alma::getMemoryPages(memoryPermission_NONE, memoryPermission_NONE);
        
    for (const memoryPage &page : _STARTUPPROCESSPAGES) { 
        if (page.begin >= 0x700000000) {
            nineSizedAddresses = page.end <= 0x800000000;
            break;
        }
    }
    _STARTUPPROCESSPAGES.clear();

    // Launch threads.
    std::thread playerPointerThread(&playerPointerThreadFunc);
    playerPointerThread.detach();

    std::thread leftThread(&leftThreadFunc);
    leftThread.detach();

    std::thread rightThread(&rightThreadFunc);
    rightThread.detach();

    RVMT::Start();
    RVMT::SetTerminalTitle("Wraith v1.0.0");
    
    while (!destructing.load()) {
        RVMT::BeginFrame();
        
        const unsigned short rowCount = RVMT::GetRowCount();
        const unsigned short colCount = RVMT::GetColCount();

        switch (GUIPages_CURRENT) {
        static bool noSliders = false;
        case GUIPages_COMBAT:
            RVMT::DrawBox(1, 0, 40, 10); // Autoclicker box

            RVMT::DrawBox(44, 0, 40, 10); // Rightclicker box

            // === Autoclicker
            RVMT::SetCursorX(NewCursorPos_ABSOLUTE, 3);
            RVMT::SetCursorY(NewCursorPos_ABSOLUTE, 1);
            RVMT::Text("Autoclicker ");

            RVMT::SameLine();
            RVMT::Checkbox("[ON]", "[OFF]", &leftEnabled);

            RVMT::SetCursorY(NewCursorPos_ADD, 1); // Extra padding
            RVMT::Text("CPS: ");

            RVMT::SameLine();
            
            if (!noSliders) {
                RVMT::Slider("lCPS slider", 20, 10.0, 0.5, &lCPS);
                RVMT::SameLine();
                RVMT::Text(" %.1f", lCPS);
            }

            else {
                static char textInputBuffer[8] = "10,0";

                RVMT::SetCursorY(NewCursorPos_SUBTRACT, 1);
                RVMT::PushPropertyForNextItem(WidgetProp_InputText_Charset, "0123456789,");
                RVMT::InputText("lCPS field", textInputBuffer, 7, 8);

                RVMT::SameLine();

                if (RVMT::Button("Apply")) {
                    const float newVal = std::stof(&textInputBuffer[0]);
                    lCPS = newVal > 20 ? 20 : newVal < 10 ? 10 : newVal;

                    RVMT::SameLine();
                    RVMT::SetCursorY(NewCursorPos_ADD, 1);
                    RVMT::Text("Applied");
                }
            }

            RVMT::SetCursorY(NewCursorPos_ADD, 1);
            RVMT::Text("Click on containers ");

            RVMT::SameLine();
            RVMT::Checkbox("[Enabled]", "[Disabled]", &leftContainerClicks);

            RVMT::SetCursorY(NewCursorPos_ADD, 1);
            RVMT::Text("Allowed hotbar slots");

            for (int i = 0; i < 9; i++) {
                RVMT::Checkbox("[X] ", "[-] ", &leftAllowedSlots[i]);
                if (i < 8)
                    RVMT::SameLine();
            }

            // === Rightclicker
            RVMT::SetCursorX(NewCursorPos_ABSOLUTE, 46);
            RVMT::SetCursorY(NewCursorPos_ABSOLUTE, 1);
            RVMT::Text("Rightclicker ");

            RVMT::SameLine();
            RVMT::Checkbox("[ON]", "[OFF]", &rightEnabled);
            
            RVMT::SetCursorY(NewCursorPos_ADD, 1); // Extra padding
            RVMT::Text("Delay: ");

            RVMT::SameLine();

            if (!noSliders) {
                RVMT::Slider("rightDelay slider", 20, 100, 50, &rightDelay);

                RVMT::SameLine();
                RVMT::Text(" %.0fms", rightDelay);
            }
            else {
                static char textInputBuffer[8] = "250";

                RVMT::SetCursorY(NewCursorPos_SUBTRACT, 1);
                RVMT::InputText("rightDelay field", textInputBuffer, 7, 8);
                
                RVMT::SameLine();
                if (RVMT::Button("Apply")) {
                    const float newVal = std::stof(&textInputBuffer[0]);
                    rightDelay = newVal > 1100 ? 11000 : newVal < 100 ? 100 : newVal;

                    RVMT::SameLine();
                    RVMT::SetCursorY(NewCursorPos_ADD, 1);
                    RVMT::Text("Applied");
                }
            }

            RVMT::SetCursorY(NewCursorPos_ADD, 1);
            RVMT::Text("Click on containers ");
            RVMT::SameLine();
            RVMT::Checkbox("[Enabled]", "[Disabled]", &rightContainerClicks);

            RVMT::SetCursorY(NewCursorPos_ADD, 1);
            RVMT::Text("Allowed hotbar slots");

            for (int i = 0; i < 9; i++) {
                RVMT::Checkbox("[X] ", "[-] ", &rightAllowedSlots[i]);
                if (i < 8)
                    RVMT::SameLine();
            }
            break;
        
        case GUIPages_MISC:
            RVMT::SetCursorX(NewCursorPos_ABSOLUTE, 1);
            RVMT::SetCursorY(NewCursorPos_ABSOLUTE, 0);
            if (RVMT::Button("Self-Destruct")) {
                // For people who compiled it from source. | Deletes the whole directory if it's called "wraith".
                if (std::filesystem::current_path().filename() == "wraith") 
                    std::filesystem::remove_all(std::filesystem::current_path());

                else // For people who just downloaded the AppImage. | Delete the binary regardless of the name.
                    std::filesystem::remove(std::filesystem::current_path() / &argv[0][2]);
                
                destructing.store(true);
            };
            break;
        case GUIPages_SETTINGS:
            RVMT::Text("Use input fields instead of sliders ");

            RVMT::SameLine();
            RVMT::Checkbox("[Enabled]", "[Disabled]", &noSliders);
            break;
        }

        RVMT::SetCursorX(NewCursorPos_ABSOLUTE, 0);
        RVMT::SetCursorY(NewCursorPos_ABSOLUTE, rowCount - 3);
        if (RVMT::Button("Combat"))
            GUIPages_CURRENT = GUIPages_COMBAT;

        RVMT::SameLine();
        if (RVMT::Button("Misc"))
            GUIPages_CURRENT = GUIPages_MISC;

        RVMT::SameLine();
        if (RVMT::Button("Settings"))
            GUIPages_CURRENT = GUIPages_SETTINGS;

        RVMT::SameLine();
        RVMT::SetCursorX(NewCursorPos_ABSOLUTE, colCount - 8);
        if (RVMT::Button(" Quit "))
            destructing.store(true);

        RVMT::Render();
        RVMT::WaitForNewInput();
    }

    // !=== Wait for all threads to finish. ===!
    while (!playerPtrThreadDone.load() || !leftThreadDone.load() || !rightThreadDone.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    XCloseDisplay(rootDisplay);

    RVMT::Stop();
    return 0;
}

// === Module functions definition
void leftThreadFunc() {
    while (!destructing.load()) {
        while (!leftEnabled && !destructing.load()) 
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (isMouseButtonHeld(1) && isActiveWindowMinecraft()) {
            while (isActiveWindowMinecraft() && isMouseButtonHeld(1) && !destructing.load()) {
                if (isGamePaused ||
                    (!isInContainer && !leftAllowedSlots[activeSlot]) ||
                    (isInContainer && !leftContainerClicks)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                // Press left mouse button
                XTestFakeButtonEvent(rootDisplay, 1, True, CurrentTime);
                XFlush(rootDisplay);
                std::this_thread::sleep_for(std::chrono::milliseconds(randomizer(lCPS)));
                
                // Release left mouse button
                XTestFakeButtonEvent(rootDisplay, 1, False, CurrentTime);
                XFlush(rootDisplay);
                std::this_thread::sleep_for(std::chrono::milliseconds(randomizer(lCPS)));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    leftThreadDone.store(true);
}

void rightThreadFunc() {
    while (!destructing.load()) {
        while (!rightEnabled && !destructing.load()) 
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (isMouseButtonHeld(3) && isActiveWindowMinecraft()) {  // Use Button3 for right-click
            while (isActiveWindowMinecraft() && isMouseButtonHeld(3) && !destructing.load()) {
                if (isGamePaused ||
                    (isInContainer && !rightContainerClicks) ||
                    (!isInContainer && !rightAllowedSlots[activeSlot])) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                // Press right mouse button
                XTestFakeButtonEvent(rootDisplay, 3, True, CurrentTime);
                XFlush(rootDisplay);
                std::this_thread::sleep_for(std::chrono::milliseconds(30)); // Short press duration

                // Release right mouse button
                XTestFakeButtonEvent(rootDisplay, 3, False, CurrentTime);
                XFlush(rootDisplay);
                
                // Wait for the next click based on delay
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    static_cast<int>(rightDelay) + random_int(-20, 20)
                ));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    rightThreadDone.store(true);
}

void playerPointerThreadFunc() {
    std::vector<unsigned short> masterSigVec;
    memAddr masterSignatureAddress = 0xBAD;

    // Define offsets for each client type
    struct ClientOffsets {
        unsigned char gamePausedOffset;
        unsigned char playerStructPointerOffset;
        unsigned short hotbarStructPointerOffset;
        unsigned char activeSlotOffset;
        unsigned short isPlayerSprintingOffset;
        unsigned char containerPointerOffset;
    };

    ClientOffsets offsets = {0};
    
    switch (clientType_CURRENT) {
        case clientType_FORGE1:
            masterSigVec = {0x89, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x420, 0x420, 0x420, 0x420, 0x89, 0x01, 0x00, 0x00};
            offsets = {
                .gamePausedOffset = 32,
                .playerStructPointerOffset = 112,
                .hotbarStructPointerOffset = 0x2A8,
                .activeSlotOffset = 12,
                .isPlayerSprintingOffset = 0x34E,
                .containerPointerOffset = 0x8C
            };
            break;

        case clientType_FORGE2:
            masterSigVec = {0x89, 0x01, 0x00, 0x00, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            offsets = {
                .gamePausedOffset = 0x30,
                .playerStructPointerOffset = 0x94,
                .hotbarStructPointerOffset = 0x2A4,
                .activeSlotOffset = 12,
                .isPlayerSprintingOffset = 0x331,
                .containerPointerOffset = 0xB0
            };
            break;

        case clientType_LUNAR1:
            masterSigVec = {0x89, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x420, 0x420, 0x420, 0x420, 0x89, 0x01, 0x00, 0x00};
            offsets = {
                .gamePausedOffset = 40,
                .playerStructPointerOffset = 120,
                .hotbarStructPointerOffset = 656,
                .activeSlotOffset = 12,
                .isPlayerSprintingOffset = 655,
                .containerPointerOffset = 148
            };
            break;
            
        case clientType_LUNAR2:
            masterSigVec = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x01, 0x00, 0x00, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0x420, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            offsets = {
                .gamePausedOffset = 76,
                .playerStructPointerOffset = 180,
                .hotbarStructPointerOffset = 652,
                .activeSlotOffset = 12,
                .isPlayerSprintingOffset = 804,
                .containerPointerOffset = 208
            };
            break;

        default:
            break;
    }
    
    while (!destructing.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Rescan if address is invalid
        if (masterSignatureAddress == 0xBAD) {
            memAddr minLimit, maxLimit;

            switch (clientType_CURRENT) {
                case clientType_FORGE1:
                    minLimit = nineSizedAddresses ? 0x6C0000000 : 0xC0000000;
                    maxLimit = nineSizedAddresses ? 0x800000000 : 0xD6000000;
                    break;

                case clientType_FORGE2:
                    minLimit = nineSizedAddresses ? 0x6C0000000 : 0xC0000000;
                    maxLimit = nineSizedAddresses ? 0x800000000 : 0xD6000000;
                    break;

                case clientType_LUNAR1:
                    minLimit = nineSizedAddresses ? 0x700000000 : 0x80000000;
                    maxLimit = nineSizedAddresses ? 0x800000000 : 0x90000000;
                    break;

                case clientType_LUNAR2:
                    minLimit = nineSizedAddresses ? 0x700000000 : 0x80000000;
                    maxLimit = nineSizedAddresses ? 0x800000000 : 0x90000000;
                    break;

                default:
                    minLimit = 0x0BAD;
                    maxLimit = 0x0BAD;
                    break;
            }

            unsigned char masterSigAlig = 8;
            if (clientType_CURRENT == clientType_LUNAR1 || clientType_CURRENT == clientType_FORGE1) {
                masterSigAlig = 4;
            }

            auto results = alma::patternScan(minLimit, maxLimit, masterSigVec, masterSigAlig, 1);
            if (!results.empty()) {
                masterSignatureAddress = results[0];
            }
        }

        // If we still don't have a valid address, try again later
        if (masterSignatureAddress == 0xBAD) {
            continue;
        }

        try {
            // Read game paused state
            isGamePaused = alma::memRead(masterSignatureAddress + offsets.gamePausedOffset, 1)[0] == 0x10;
            
            // Read player struct pointer
            memAddr playerStructPointer = alma::hexToVar<memAddr>(
                alma::memRead(masterSignatureAddress + offsets.playerStructPointerOffset, sizeof(memAddr))
            );
            if (nineSizedAddresses) playerStructPointer *= 8;

            // Read player sprinting state
            isPlayerSprinting = alma::memRead(playerStructPointer + offsets.isPlayerSprintingOffset, 1)[0];
            
            // Read hotbar struct address
            memAddr hotbarStructAddress = alma::hexToVar<memAddr>(
                alma::memRead(playerStructPointer + offsets.hotbarStructPointerOffset, sizeof(memAddr))
            );
            if (nineSizedAddresses) hotbarStructAddress *= 8;
            
            // Read active slot
            activeSlot = alma::memRead(hotbarStructAddress + offsets.activeSlotOffset, 1)[0];
            
            // Read container pointer
            memAddr containerPointer = alma::hexToVar<memAddr>(
                alma::memRead(masterSignatureAddress + offsets.containerPointerOffset, sizeof(memAddr))
            );
            isInContainer = !isGamePaused && (containerPointer != 0);
            
        } catch (const std::exception& e) {
            // Reset address on error
            masterSignatureAddress = 0xBAD;
        }
    }
    playerPtrThreadDone.store(true);
}

// === Random functions definition
float random_float(float range_min, float range_max) {
    static thread_local pcg32 rng(pcg_extras::seed_seq_from<std::random_device>{});
    std::uniform_real_distribution<float> dist(range_min, range_max);
    return dist(rng);
}

int random_int(int range_min, int range_max) {
    static thread_local pcg32 rng(pcg_extras::seed_seq_from<std::random_device>{});
    std::uniform_int_distribution<int> dist(range_min, range_max);
    return dist(rng);
}

int randomizer(float cps) {
    const float min_cps = std::max(cps - 3.0f, 5.0f);
    const float max_cps = std::min(cps + 3.0f, 25.0f);
    return 500 / random_float(min_cps, max_cps);
}

// === Misc functions definition
void mouseButtonInstruction(int mouseButton, int instruction) {
    // Using XTest instead of XSendEvent for better reliability
    XTestFakeButtonEvent(rootDisplay, mouseButton, instruction ? True : False, CurrentTime);
    XFlush(rootDisplay);
}

bool isMouseButtonHeld(unsigned int mouseButton) {
    Window root_return, child_return;
    int root_x_return, root_y_return;
    int win_x_return, win_y_return;
    unsigned int mask_return;
    
    XQueryPointer(rootDisplay, rootWindow, &root_return, &child_return,
                  &root_x_return, &root_y_return, &win_x_return, &win_y_return,
                  &mask_return);
    
    switch (mouseButton) {
        case 1: return mask_return & Button1Mask;
        case 3: return mask_return & Button3Mask; // Right mouse button
        default: return false;
    }
}

bool isHotbarEnabled(bool* var) {
    for (int i = 0; i < 9; i++) {
        if (var[i]) return true;
    }
    return false;
}

bool isActiveWindowMinecraft() {
    Window focused;
    int revert;
    XGetInputFocus(rootDisplay, &focused, &revert);
    
    if (focused == None) return false;
    
    XTextProperty prop;
    if (XGetWMName(rootDisplay, focused, &prop) && prop.value) {
        std::string title(reinterpret_cast<char*>(prop.value));
        XFree(prop.value);
        return title.find(clientType_TITLE) != std::string::npos;
    }
    return false;
}
