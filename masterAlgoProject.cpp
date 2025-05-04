#include <iostream>
#include <string>
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <regex>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include "httplib.h"
#include "json.hpp"
#include <chrono>
using json = nlohmann::json;

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// #pragma comment(lib, "libcurl.lib") // If using static libcurl

struct YouTubeVideo {
    std::string title;
    std::string videoId;
    std::string captions;
};

struct Recipe {
    std::string title;
    std::string ingredients;
    std::string instructions;
    std::vector<YouTubeVideo> youtubeVideos; // Add YouTube videos
};


// --- Function Declarations (Prototypes - Good Practice) ---
std::string url_encode(const std::string& value);
bool isLikelyRecipeResult(const std::string& url, const std::string& title, const std::string& snippet);
std::string get_request(const std::string& url);
SOCKET connectToSlave(const std::string& slaveIp, int slavePort);
bool sendDataWithSize(SOCKET sock, const std::string& data, const std::string& dataType);
std::string receiveDataWithSize(SOCKET sock, const std::string& dataType);
std::string receiveData(SOCKET sock); // receiveData from scraper slave.
void processRecipeUrls(
    const std::string& scraperSlaveIp, int scraperSlavePort,
    const std::string& aiSlaveIp, int aiSlavePort,
    const std::vector<std::string>& urls,
    Recipe& recipe);
std::vector<YouTubeVideo> getYoutubeVideos(const std::string& query,
    const std::string& youtubeSlaveIp, int youtubeSlavePort);
size_t WriteCallbackMaster(void* contents, size_t size, size_t nmemb, void* userp);

// --- New Function: Process Query from API ---
std::vector<Recipe> processQuery(const std::string& query,
    const std::string& scraperSlaveIp, int scraperSlavePort,
    const std::string& aiSlaveIp, int aiSlavePort,
    const std::string& youtubeSlaveIp, int youtubeSlavePort) {

    std::vector<Recipe> recipes;
    std::string baseQuery = query;
    std::string queryModifiers = " recipe intitle:recipe -site:youtube.com -site:pinterest.com -site:amazon.* -site:wikipedia.org";
    std::string fullQuery = baseQuery + queryModifiers;
    std::string googleApiKey = "AIzaSyANo1-NDll1AS2ubft1Hrg9lKnJTVtyVlE"; // Use your Google Search key
    std::string googleCx = "522924fa8320e405f";       // Use your Google Search CX ID

    int maxResults = 3; // Limit for testing

    // --- Get URLs from Google Search ---
    std::string encodedQuery = url_encode(fullQuery);
    std::string searchUrl = "https://www.googleapis.com/customsearch/v1?q=" + encodedQuery +
        "&key=" + googleApiKey + "&cx=" + googleCx;

    std::cout << "Using Google Search URL: " << searchUrl << std::endl;
    std::string jsonResponse = get_request(searchUrl);

    if (jsonResponse.empty()) {
        std::cerr << "Failed to get a response from Google Custom Search." << std::endl;
        return recipes; // return empty vector
    }

    // --- Extract Links using Enhanced Filtering ---
    std::vector<std::string> recipeLinks;
    try {
        std::regex itemRegex(
            "\"link\":\\s*\"(https?://[^\"]+)\""
            "[\\s\\S]*?"
            "\"title\":\\s*\"([^\"]+)\""
            "[\\s\\S]*?"
            "\"snippet\":\\s*\"([\\s\\S]*?)\""
        );
        std::sregex_iterator iter(jsonResponse.begin(), jsonResponse.end(), itemRegex);
        std::sregex_iterator end;
        for (; iter != end; ++iter) {
            std::smatch match = *iter;
            if (match.size() >= 4) {
                std::string url = match[1].str();
                std::string title = match[2].str();
                std::string snippet = match[3].str();
                if (isLikelyRecipeResult(url, title, snippet)) {
                    if (recipeLinks.size() < maxResults) {
                        recipeLinks.push_back(url);
                        std::cout << "Found likely recipe: [" << title << "] " << url << std::endl;
                    }
                    else break;
                }
            }
        }
    }
    catch (const std::regex_error& e) {
        std::cerr << "Regex error parsing search results: " << e.what() << std::endl;
        return recipes; // return empty vector
    }

    if (recipeLinks.empty()) {
        std::cerr << "No likely recipe URLs found after filtering." << std::endl;
        return recipes; // return empty vector
    }

    // --- Get YouTube Videos based on prompt ---
    std::vector<YouTubeVideo> youtubeVideos = getYoutubeVideos(query + " recipe", youtubeSlaveIp, youtubeSlavePort);
    // --- Process URLs with All Slaves ---
    for (int i = 0; i < recipeLinks.size(); ++i) {
        Recipe recipe;
        std::string filename = "recipe_text_" + std::to_string(i + 1) + ".txt";
        recipe.title = "Recipe " + std::to_string(i + 1);
        processRecipeUrls(scraperSlaveIp, scraperSlavePort, aiSlaveIp, aiSlavePort, recipeLinks, recipe);
        if (i < youtubeVideos.size()) {
            recipe.youtubeVideos.push_back(youtubeVideos[i]);
        }

        // Save results to text file
        std::ofstream outFile(filename);
        if (outFile.is_open()) {
            outFile << "Ingredients: \n" << recipe.ingredients << "\n";
            outFile << "Instructions: \n" << recipe.instructions << "\n";
            outFile << "Youtube Videos: \n";
            for (auto& video : recipe.youtubeVideos) {
                outFile << "Title: " << video.title << "\n";
                outFile << "Video ID: " << video.videoId << "\n";
                outFile << "Captions: \n" << video.captions << "\n";
            }
            outFile.close();
        }
        else {
            std::cerr << "Failed to open file '" << filename << "' for writing." << std::endl;
        }

        recipes.push_back(recipe);
    }
    // if there is less than 3 recipes get additional youtube videos
    if (recipes.size() < 3) {
        for (int i = recipes.size(); i < 3 && i < youtubeVideos.size(); i++) {
            Recipe recipe;
            recipe.title = "Recipe Video" + std::to_string(i + 1);
            recipe.youtubeVideos.push_back(youtubeVideos[i]);
            recipes.push_back(recipe);
        }
    }
    return recipes;
}

std::vector<Recipe> processQueryAndGetRecipes(
    const std::string& query,
    const std::string& scraperSlaveIp, int scraperSlavePort,
    const std::string& aiSlaveIp, int aiSlavePort,
    const std::string& youtubeSlaveIp, int youtubeSlavePort)
{
    // Call the existing processQuery (it already processes everything)
    std::vector<Recipe> recipes = processQuery(query, scraperSlaveIp, scraperSlavePort, aiSlaveIp, aiSlavePort, youtubeSlaveIp, youtubeSlavePort);
  
    return recipes;
}

void startAPIServer(const std::string& scraperSlaveIp, int scraperSlavePort,
    const std::string& aiSlaveIp, int aiSlavePort,
    const std::string& youtubeSlaveIp, int youtubeSlavePort) {

    httplib::Server svr;

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::ifstream file("code.html");
        if (file) {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            res.set_content(content, "text/html");
        }
        else {
            res.set_content("Error loading HTML page", "text/plain");
        }
        });

    svr.Post("/search", [=](const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_body = json::parse(req.body);
            std::string query = json_body["query"];

            std::cout << "Received search request for: " << query << std::endl;

            // Call processQueryAndGetRecipes
            std::vector<Recipe> recipes = processQuery(query, scraperSlaveIp, scraperSlavePort, aiSlaveIp, aiSlavePort, youtubeSlaveIp, youtubeSlavePort);

            // Fetch YouTube videos once based on the query
            std::vector<YouTubeVideo> youtubeVideos = getYoutubeVideos(query + " recipe", youtubeSlaveIp, youtubeSlavePort);

            json response;
            response["status"] = "success";

            response["recipes"] = json::array();
            for (const auto& recipe : recipes) {
                json recipeJson;
                recipeJson["title"] = recipe.title;
                recipeJson["ingredients"] = recipe.ingredients;
                recipeJson["instructions"] = recipe.instructions;

                // Add youtube videos to recipe
                recipeJson["videos"] = json::array();
                for (const auto& video : recipe.youtubeVideos) {
                    json videoJson;
                    videoJson["title"] = video.title;
                    videoJson["videoId"] = video.videoId;
                    videoJson["captions"] = video.captions;
                    recipeJson["videos"].push_back(videoJson);
                }
                response["recipes"].push_back(recipeJson);
            }

            // Build videos array for separate display of all youtube videos
            response["videos"] = json::array();
            for (const auto& video : youtubeVideos) {
                json videoJson;
                videoJson["title"] = video.title;
                videoJson["videoId"] = video.videoId;
                videoJson["captions"] = video.captions;
                response["videos"].push_back(videoJson);
            }

            std::cout << response.dump(2) << std::endl; // Pretty print to console
            res.set_content(response.dump(), "application/json");

        }
        catch (const std::exception& e) {
            res.status = 400;
            res.set_content(R"({"status": "error", "message": "Invalid request"})", "application/json");
        }
        });


    std::cout << "API server running on port 8080..." << std::endl;
    svr.listen("0.0.0.0", 8080);
}




// --- Socket Helper Function (Connect) ---
SOCKET connectToSlave(const std::string& slaveIp, int slavePort) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed for " << slaveIp << ":" << slavePort << " : " << WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(slavePort);
    if (inet_pton(AF_INET, slaveIp.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address for " << slaveIp << ":" << slavePort << std::endl;
        closesocket(sock);
        return INVALID_SOCKET;
    }

    std::cout << "Connecting to slave at " << slaveIp << ":" << slavePort << "..." << std::endl;
    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed to " << slaveIp << ":" << slavePort << " : " << WSAGetLastError() << std::endl;
        closesocket(sock);
        return INVALID_SOCKET;
    }
    std::cout << "Connected to " << slaveIp << ":" << slavePort << "." << std::endl;
    return sock;
}

// --- Socket Helper Function (Send Data with Size Prefix) ---
bool sendDataWithSize(SOCKET sock, const std::string& data, const std::string& dataType) {
    uint32_t dataSize = data.size();
    uint32_t netSize = htonl(dataSize);

    // Send size
    int sizeSent = send(sock, reinterpret_cast<const char*>(&netSize), sizeof(netSize), 0);
    if (sizeSent != sizeof(netSize)) {
        std::cerr << "Send failed (size - " << dataType << "): " << WSAGetLastError() << std::endl;
        return false;
    }

    // Send data
    if (dataSize > 0) {
        int totalSent = 0;
        int bytesLeft = dataSize;
        const char* ptr = data.c_str();
        while (bytesLeft > 0) {
            int dataSent = send(sock, ptr + totalSent, bytesLeft, 0);
            if (dataSent == SOCKET_ERROR) {
                std::cerr << "Send failed (data - " << dataType << "): " << WSAGetLastError() << std::endl;
                return false;
            }
            if (dataSent == 0) {
                std::cerr << "Send returned 0 (data - " << dataType << "), connection closed?" << std::endl;
                return false;
            }
            totalSent += dataSent;
            bytesLeft -= dataSent;
        }
        if (totalSent != dataSize) {
            std::cerr << "Send incomplete (data - " << dataType << ", sent " << totalSent << "/" << dataSize << ")" << std::endl;
            return false;
        }
    }
    std::cout << "Sent " << dataType << " (" << dataSize << " bytes)." << std::endl;
    return true;
}

// --- Socket Helper Function (Receive Data with Size Prefix) ---
std::string receiveDataWithSize(SOCKET sock, const std::string& dataType) {
    std::string receivedData;
    uint32_t netSize = 0;
    int sizeBytesReceived = recv(sock, reinterpret_cast<char*>(&netSize), sizeof(netSize), MSG_WAITALL);

    if (sizeBytesReceived == SOCKET_ERROR) {
        std::cerr << "Recv failed (size - " << dataType << "): " << WSAGetLastError() << std::endl;
        return ""; // Return empty on error
    }
    else if (sizeBytesReceived == 0) {
        std::cerr << "Slave closed connection while sending size (" << dataType << ")." << std::endl;
        return "";
    }
    else if (sizeBytesReceived != sizeof(netSize)) {
        std::cerr << "Error: Received incomplete size (" << dataType << ": " << sizeBytesReceived << "/" << sizeof(netSize) << " bytes)." << std::endl;
        return "";
    }

    uint32_t dataSize = ntohl(netSize);
    std::cout << "Expecting " << dataSize << " bytes of " << dataType << " data." << std::endl;

    if (dataSize > 0) {
        receivedData.resize(dataSize); // Pre-allocate buffer
        int totalBytesReceived = 0;
        while (totalBytesReceived < dataSize) {
            // Read directly into the string's buffer
            int bytesReceived = recv(sock, &receivedData[totalBytesReceived], dataSize - totalBytesReceived, 0);
            if (bytesReceived == SOCKET_ERROR) {
                std::cerr << "Recv failed (data - " << dataType << "): " << WSAGetLastError() << std::endl;
                return ""; // Return empty on error
            }
            else if (bytesReceived == 0) {
                std::cerr << "Slave closed connection prematurely during data receive (" << dataType << ")." << std::endl;
                return "";
            }
            totalBytesReceived += bytesReceived;
        }
        if (totalBytesReceived != dataSize) {
            std::cerr << "Error: Received incomplete data (" << dataType << ": " << totalBytesReceived << "/" << dataSize << " bytes)." << std::endl;
            return "";
        }
        std::cout << "Successfully received " << receivedData.size() << " bytes of " << dataType << " data." << std::endl;
    }
    else {
        std::cout << "Received size 0 for " << dataType << ". No data." << std::endl;
    }
    return receivedData;
}

// --- Socket Helper Function (Receive Data from Scraper Slave without size prefix) ---
std::string receiveData(SOCKET sock) {
    std::string receivedData;
    char buffer[4096];
    int bytesReceived;

    do {
        bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0); //Leave room for null terminator
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0'; //Null-terminate the received data
            receivedData += buffer;
        }
        else if (bytesReceived == SOCKET_ERROR) {
            std::cerr << "Recv failed: " << WSAGetLastError() << std::endl;
            return ""; //Return empty on error
        }
        else {
            //Connection closed by the client
            std::cout << "Connection closed by the client." << std::endl;
            break; //Exit the loop
        }
    } while (bytesReceived == sizeof(buffer) - 1); //Continue if the buffer was full

    return receivedData;
}

// --- Get YouTube Videos from Slave ---
std::vector<YouTubeVideo> getYoutubeVideos(const std::string& query,
    const std::string& youtubeSlaveIp, int youtubeSlavePort) {
    std::vector<YouTubeVideo> youtubeVideos;
    SOCKET youtubeSock = connectToSlave(youtubeSlaveIp, youtubeSlavePort);
    if (youtubeSock != INVALID_SOCKET) {
        if (sendDataWithSize(youtubeSock, query, "YouTube Query")) {
            std::string youtubeResponse = receiveDataWithSize(youtubeSock, "YouTube Data");
            if (!youtubeResponse.empty()) {
                try {
                    json youtubeJson = json::parse(youtubeResponse);
                    if (youtubeJson.contains("videos") && youtubeJson["videos"].is_array()) {
                        for (auto& videoJson : youtubeJson["videos"]) {
                            YouTubeVideo video;
                            video.title = videoJson["title"].get<std::string>();
                            video.videoId = videoJson["videoId"].get<std::string>();
                            video.captions = videoJson["captions"].get<std::string>();
                            youtubeVideos.push_back(video);
                        }
                    }
                }
                catch (json::parse_error& e) {
                    std::cerr << "JSON parsing failed for YouTube response: " << e.what() << std::endl;
                }
            }
            else {
                std::cerr << "Receive from YouTube Slave failed or returned empty data." << std::endl;
            }
        }
        closesocket(youtubeSock);
        std::cout << "Disconnected from YouTube Slave." << std::endl;
    }
    else {
        std::cerr << "Failed to connect to YouTube Slave. Skipping YouTube extraction." << std::endl;
    }
    return youtubeVideos;
}

// --- Main Processing Function (Handles All Slaves) ---
void processRecipeUrls(
    const std::string& scraperSlaveIp, int scraperSlavePort,
    const std::string& aiSlaveIp, int aiSlavePort,
    const std::vector<std::string>& urls,
    Recipe& recipe)
{
    WSADATA wsa;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed in processUrlsWithSlaves: " << iResult << std::endl;
        return;
    }

    int urlCounter = 0;
    for (const std::string& url : urls) {
        urlCounter++;
        std::cout << "\n--- Processing URL " << urlCounter << ": " << url << " ---" << std::endl;

        std::string cleanedHtml;
        std::string recipeText;
        bool scraperSuccess = false;

        // --- Stage 1: Scraper Slave ---
        SOCKET scraperSock = connectToSlave(scraperSlaveIp, scraperSlavePort);
        if (scraperSock != INVALID_SOCKET) {
            // Send URL
            iResult = send(scraperSock, url.c_str(), url.length(), 0);
            if (iResult == SOCKET_ERROR) {
                std::cerr << "Send failed (URL to Scraper): " << WSAGetLastError() << std::endl;
            }
            else {
                std::cout << "Sent URL to Scraper." << std::endl;
                // Send Command
                std::string command = "SEND_DATA";
                iResult = send(scraperSock, command.c_str(), command.length(), 0);
                if (iResult == SOCKET_ERROR) {
                    std::cerr << "Send failed (Command to Scraper): " << WSAGetLastError() << std::endl;
                }
                else {
                    std::cout << "Sent '" << command << "' to Scraper." << std::endl;
                    // Receive Cleaned HTML
                    cleanedHtml = receiveDataWithSize(scraperSock, "Cleaned HTML");
                    if (!cleanedHtml.empty()) {
                        std::cout << "Received cleaned HTML for URL " << url << " (" << cleanedHtml.size() << " bytes): " << cleanedHtml.substr(0, 200) << "..." << std::endl;
                    }
                    else {
                        std::cout << "Received empty cleaned HTML for URL " << url << std::endl;
                    }
                    // Check if receiveDataWithSize returned empty string due to error or actual 0 size
                    if (!cleanedHtml.empty() || (cleanedHtml.empty() && WSAGetLastError() == 0)) {
                        // Considered success if we received data, or if we received size 0 without a socket error
                        scraperSuccess = true;
                        if (cleanedHtml.empty()) {
                            std::cout << "(Scraper returned 0 bytes)" << std::endl;
                        }
                    }
                    else {
                        // receiveDataWithSize returned empty due to an error
                        std::cerr << "Receive from Scraper failed." << std::endl;
                    }
                }
            }
            closesocket(scraperSock); // Disconnect after use
            std::cout << "Disconnected from Scraper Slave." << std::endl;
        }
        else {
            std::cerr << "Failed to connect to Scraper Slave. Skipping." << std::endl;
        }

        // --- Stage 2: AI Slave (Only if Scraper was successful and got HTML) ---
        bool aiSuccess = false;
        if (scraperSuccess && !cleanedHtml.empty()) { // Don't call AI for empty HTML
            SOCKET aiSock = connectToSlave(aiSlaveIp, aiSlavePort);
            if (aiSock != INVALID_SOCKET) {
                // Send Cleaned HTML
                if (sendDataWithSize(aiSock, cleanedHtml, "Cleaned HTML")) {
                    // Receive Recipe Text
                    recipeText = receiveDataWithSize(aiSock, "Recipe Text");
                    if (!recipeText.empty()) {
                        std::cout << "Received recipe text for URL " << url << " (" << recipeText.size() << " bytes): " << recipeText.substr(0, 400) << "..." << std::endl;
                    }
                    else {
                        std::cout << "Received empty recipe text for URL " << url << std::endl;
                    }
                    // Check if receiveDataWithSize returned empty string due to error or actual 0 size
                    if (!recipeText.empty() || (recipeText.empty() && WSAGetLastError() == 0)) {
                        aiSuccess = true;
                        if (recipeText.empty()) {
                            std::cout << "(AI Slave returned 0 bytes)" << std::endl;
                        }
                    }
                    else {
                        // receiveDataWithSize returned empty due to an error
                        std::cerr << "Receive from AI Slave failed." << std::endl;
                    }
                }
                closesocket(aiSock); // Disconnect after use
                std::cout << "Disconnected from AI Slave." << std::endl;
            }
            else {
                std::cerr << "Failed to connect to AI Slave. Skipping AI extraction." << std::endl;
            }
        }
        else if (!scraperSuccess) {
            std::cerr << "Skipping AI Slave due to Scraper failure." << std::endl;
        }
        else { // scraperSuccess was true, but cleanedHtml was empty
            std::cout << "Skipping AI Slave because Scraper returned empty HTML." << std::endl;
        }

        // --- Stage 4: Save Results---
        if (aiSuccess) {
            if (recipeText == "NO_RECIPE_FOUND") {
                std::cout << "AI Slave indicated no recipe found for: " << url << std::endl;
            }
            else if (recipeText.rfind("Error:", 0) == 0) { // Check if response starts with "Error:"
                std::cerr << "AI Slave returned an error: " << recipeText << std::endl;
            }
            else if (recipeText.empty()) {
                std::cout << "AI Slave returned empty response (not error, not 'NO_RECIPE_FOUND'). Treating as no recipe." << std::endl;
            }
            else {
                std::cout << "Successfully received recipe text (" << recipeText.size() << " bytes)." << std::endl;
                recipe.ingredients = "Ingredients: \n" + recipeText;
                recipe.instructions = "Instructions: \n" + recipeText;
            }
        }
        else {
            std::cerr << "Failed to get recipe text from AI Slave for URL: " << url << std::endl;
        }
        // Optional delay between processing URLs
        // std::this_thread::sleep_for(std::chrono::seconds(1));

    } // End of URL loop

    WSACleanup(); // Cleanup Winsock started in this function
    std::cout << "Master finished processing all URLs." << std::endl;
}

// --- URL Encoding ---
std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (const unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        }
        else {
            escaped << '%' << std::setw(2) << int(c);
        }
    }
    return escaped.str();
}

// --- Recipe Filtering ---
bool isLikelyRecipeResult(const std::string& url, const std::string& title, const std::string& snippet) {
    std::vector<std::string> titleKeywords = { "recipe", "how to make", "cook", "directions", "palao", "pulao" };
    std::vector<std::string> snippetKeywords = { "ingredients", "instructions", "steps", "preparation", "directions", "cup", "tsp", "tbsp", "grams", "oz", "preheat", "bake", "fry", "boil", "simmer", "chop", "dice" };
    std::vector<std::string> negativeKeywords = { "review", "history", "nutrition facts", "calories", "video", "shop", "buy", "restaurant", "news", "about", "story" };

    std::string lowerTitle = title;
    std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
    std::string lowerSnippet = snippet;
    std::transform(lowerSnippet.begin(), lowerSnippet.end(), lowerSnippet.begin(), ::tolower);

    for (const auto& negWord : negativeKeywords) {
        if (lowerTitle.find(negWord) != std::string::npos) return false;
        // if (lowerSnippet.find(negWord) != std::string::npos) return false; // Optional stricter check
    }

    bool titleMatch = false;
    for (const auto& keyword : titleKeywords) {
        if (lowerTitle.find(keyword) != std::string::npos) { titleMatch = true; break; }
    }

    bool snippetMatch = false;
    for (const auto& keyword : snippetKeywords) {
        if (lowerSnippet.find(keyword) != std::string::npos) { snippetMatch = true; break; }
    }

    bool listPatternMatch = (snippet.find("1.") != std::string::npos && snippet.find("2.") != std::string::npos) ||
        (snippet.find("- ") != std::string::npos) ||
        (snippet.find("\xe2\x80\xa2 ") != std::string::npos);

    return titleMatch || (snippetMatch && listPatternMatch);
}

// --- Libcurl HTTP GET Request ---
size_t WriteCallbackMaster(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = (std::string*)userp;
    mem->append((char*)contents, realsize);
    return realsize;
}
std::string get_request(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string result;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackMaster);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); // Often needed for HTTPS
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L); // Often needed for HTTPS
        // Consider setting CURLOPT_CAINFO or CURLOPT_CAPATH if SSL verification fails
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "MasterAlgoProject/1.0");

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed for %s: %s\n", url.c_str(), curl_easy_strerror(res));
            result = ""; // Clear on error
        }
        else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code != 200) {
                std::cerr << "HTTP GET request failed for " << url << " with code: " << http_code << std::endl;
                // Consider how to handle non-200 responses (e.g., clear result?)
                // result = ""; // Optionally clear if not 200
            }
            else {
                std::cout << "Successfully received response (HTTP " << http_code << ") from: " << url << std::endl;
            }
        }
        curl_easy_cleanup(curl);
    }
    else {
        std::cerr << "Failed to initialize CURL." << std::endl;
    }
    return result;
}


int main() {
   
    try {
        // Initialize CURL globally
        CURLcode curl_init_res = curl_global_init(CURL_GLOBAL_ALL);
        if (curl_init_res != CURLE_OK) {
            std::cerr << "curl_global_init() failed: " << curl_easy_strerror(curl_init_res) << std::endl;
            return 1;
        }

        // Slave Configuration
        std::string scraperSlaveIp = "127.0.0.1";
        int scraperSlavePort = 8084;
        std::string aiSlaveIp = "127.0.0.1";
        int aiSlavePort = 8081;
        std::string youtubeSlaveIp = "127.0.0.1";
        int youtubeSlavePort = 8083;

        // Create httplib server instance
        httplib::Server svr;

        // Setting base dir for images
        svr.set_base_dir(".");


        // Start the API server
        startAPIServer(scraperSlaveIp, scraperSlavePort, aiSlaveIp, aiSlavePort, youtubeSlaveIp, youtubeSlavePort);

        // Cleanup (this will only be reached if the server stops)
        curl_global_cleanup();
        
        return 0;

    }
    catch (const std::exception& e) {
        std::cerr << "Unhandled exception in main: " << e.what() << std::endl;
       
        return 1;
    }
}