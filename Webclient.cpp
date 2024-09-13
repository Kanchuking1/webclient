/*
    Name: Wahib Sabir Kapdi
    Class: CSCE 612
    Semester: Graduate Student - MCS - Fall 2024 - 1st Semester
*/

#include "pch.h"
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int INITIAL_BUFFER_SIZE = 4096;
const int MAX_ROBOTS_SIZE = 8*16e3;
const int MAX_PAGE_SIZE = 16e6;
const int MAX_REQUEST_TIME = 1e5;
const string USER_AGENT = "wahibsCrawler/1.1";

set<string> seenHosts;
set<DWORD> seenIPs;

queue<string> urlsToParse;
mutex mLock;

map<string, bool> parsedUrls;

int activeThreads = 0, Q = 0, E = 0, H = 0, D = 0, I = 0, R = 0, C = 0, L = 0;
bool onGoing = true, logOutput = true;

int pagesSinceLastWake = 0, bytesSinceLastWake = 0, totalBytes = 0;

map<char, int> statusCodeMap;

class Url {
public:
    string path = "/", scheme = "http", host, port = "80", query, fragment;

    Url() {
        query = "";
        fragment = "";
        host = "";
    }

    Url(string _path, string _host, string _port, string _query, string _fragment) {
        path = _path;
        host = _host;
        port = _port;
        query = _query;
        fragment = _fragment;
    }

    static Url* Parse(string url) {
        Url result;

        if (url.length() == 0) {
            return new Url();
        }

        string scheme = url.substr(0, 4);
        if (scheme.compare("http") != 0) {
            logOutput && cout << "failed with invalid scheme" << endl;
            return nullptr;
        }

        string currentUrl = url.substr(7, url.size());

        size_t fragmentStart = currentUrl.find('#');
        if (fragmentStart != string::npos) {
            result.fragment = currentUrl.substr(fragmentStart + 1, currentUrl.size());
            currentUrl = currentUrl.substr(0, fragmentStart);
        }

        size_t queryStart = currentUrl.find('?');
        if (queryStart != string::npos) {
            result.query = currentUrl.substr(queryStart + 1);
            currentUrl = currentUrl.substr(0, queryStart);
        }

        size_t hostEnd = currentUrl.find('/');

        if (!(hostEnd == string::npos)) {
            if (hostEnd != (currentUrl.size() - 1)) {
                result.path = currentUrl.substr(hostEnd);
            }
            currentUrl = currentUrl.substr(0, hostEnd);
        }

        size_t portStart = currentUrl.find(':');

        if (portStart != string::npos) {
            result.port = currentUrl.substr(portStart + 1);
            currentUrl = currentUrl.substr(0, portStart);
        }

        if (result.port.compare("") == 0 || atoi(result.port.c_str()) < 1 || atoi(result.port.c_str()) > 65535) {
            logOutput && cout << "failed with invalid port" << endl;
            return nullptr;
        }

        result.host = currentUrl;

        return new Url(result.path, result.host, result.port, result.query, result.fragment);
    }
};

string to_string(Url url) {
    return "path: " + url.path
        + "\nscheme: " + url.scheme
        + "\nhost: " + url.host
        + "\nport: " + url.port
        + "\nquery: " + url.query
        + "\nfragment: " + url.fragment;
}

string connectToServer(Url uri, sockaddr_in& server, string httpMethod, string connectingOn, int maxSize, string path = "") {
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(uri.port.c_str()));
    WSADATA wsaData;

    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        logOutput&& printf("WSAStartup error %d\n", WSAGetLastError());
        WSACleanup();
        return "";
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        logOutput&& printf("socket() generated error %d\n", WSAGetLastError());
        WSACleanup();
        return "";
    }

    if (path.size() == 0) {
        logOutput&& cout << "      * Connecting on " << connectingOn << "... ";
    }
    else {
        logOutput&& cout << "\tConnecting on " << connectingOn << "... ";
    }

    DWORD timeout1 = MAX_REQUEST_TIME;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout1, sizeof(timeout1));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout1, sizeof(timeout1));

    auto start = chrono::high_resolution_clock::now();

    if (connect(sock, (struct sockaddr*)&server, sizeof(struct sockaddr_in)) == SOCKET_ERROR)
    {
        logOutput&& cout << "failed with " << WSAGetLastError() << endl;
        closesocket(sock);
        WSACleanup();
        return "";
    }

    auto end = chrono::high_resolution_clock::now();
    auto time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    logOutput&& cout << "done in " << time_elapsed << " ms" << endl;

    start = chrono::high_resolution_clock::now();
    logOutput&& cout << "\tLoading... ";

    auto recvBuffer = new char[INITIAL_BUFFER_SIZE];
    int allocatedSize = INITIAL_BUFFER_SIZE;
    int curPos = 0;

    FD_SET readFD;
    FD_SET writeFD;
    FD_SET exceptFD;

    FD_ZERO(&readFD);
    FD_ZERO(&writeFD);
    FD_ZERO(&exceptFD);

    FD_SET(sock, &readFD);
    FD_SET(sock, &writeFD);
    FD_SET(sock, &exceptFD);

    timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    int ret;

    string part1 = path.size() > 0 ? path : uri.path;

    if (path.size() == 0 && uri.query.size() > 0) {
        part1 += "?" + uri.query;
    }

    string request = ((string)(httpMethod + " " + part1 + " HTTP/1.0\r\nHost: " + uri.host + "\r\nUser-agent: " + USER_AGENT + "\r\nConnection: close\r\n\r\n")).c_str();

    if (send(sock, request.c_str(), request.size(), 0) == SOCKET_ERROR) {
        logOutput&& cout << "failed with " << WSAGetLastError() << endl;
        return "";
    }

    while (true)
    {
        // wait to see if socket has any data (see MSDN)
        ret = select(0, &readFD, &writeFD, &exceptFD, &timeout);

        if (ret > 0)
        {
            // new data available; now read the next segment
            int bytes = recv(sock, recvBuffer + curPos, allocatedSize - curPos, 0);
            end = chrono::high_resolution_clock::now();
            time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();

            if (bytes == SOCKET_ERROR) {
                if (WSAGetLastError() != WSAETIMEDOUT) {
                    logOutput&& cout << "failed with slow download\n";
                    closesocket(sock);
                    WSACleanup();
                }
                else {
                    logOutput&& cout << "failed with " << WSAGetLastError() << " on recv" << endl;
                    closesocket(sock);
                    WSACleanup();
                }
                return "";
            }

            curPos += bytes;
            totalBytes += bytes;
            bytesSinceLastWake += bytes;

            if (bytes == 0) {
                recvBuffer[curPos] = '\0';
                break;
            }

            if (curPos >= maxSize) {
                logOutput&& cout << "failed with exceeding max" << endl;
                closesocket(sock);
                WSACleanup();
                return "";
            }

            if (time_elapsed > 15000) {
                logOutput&& cout << "failed with slow download\n";
                closesocket(sock);
                WSACleanup();
                return "";
            }

            if ((allocatedSize - curPos) < INITIAL_BUFFER_SIZE) {
                char* newBuffer = new char[allocatedSize * 2];
                strcpy_s(newBuffer, allocatedSize * 2, recvBuffer);
                delete[] recvBuffer;
                recvBuffer = newBuffer;
                allocatedSize *= 2;
            }
        }
        else if (ret == SOCKET_ERROR) {
            logOutput&& cout << "failed with " << WSAGetLastError() << endl;
            closesocket(sock);
            WSACleanup();
            return "";
        }
        else
        {
            logOutput&& cout << "failed with timeout" << endl;
            closesocket(sock);
            WSACleanup();
            return "";
        }
    }

    closesocket(sock);
    WSACleanup();

    if (curPos < 6) {
        logOutput&& cout << "failed with non-HTTP header (does not begin with HTTP/)" << endl;
        return "";
    }
    string headerStart = ((string)recvBuffer).substr(0, 5);

    if (headerStart.compare("HTTP/") != 0) {
        logOutput&& cout << "failed with non-HTTP header (does not begin with HTTP/)" << endl;
        return "";
    }

    end = chrono::high_resolution_clock::now();
    time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
    logOutput&& cout << "done in " << time_elapsed << " ms with " << curPos << " bytes" << endl;

    string result = string(recvBuffer, recvBuffer + curPos);

    delete recvBuffer;

    return result;
}

bool verifyHeader(string result, char acceptanceCode) {
    string headers = result.substr(0, result.find("\r\n\r\n"));
    string statusCode = headers.substr(9, 3);
    logOutput&& cout << "\tVerifying header... status code " << statusCode << endl;
    if (statusCode.size() == 3 && statusCode[0] == acceptanceCode) {
        return true;
    }

    return false;
}

bool checkHostUniqueness(string host) {
    logOutput&& cout << "\tChecking host uniqueness... ";
    mLock.lock();
    auto hostCheck = seenHosts.insert(host);

    if (hostCheck.second == true) {
        logOutput&& cout << "passed" << endl;
        H++;
        mLock.unlock();
        return true;
    }
    else {
        logOutput&& cout << "failed" << endl;
        mLock.unlock();
        return false;
    }
}

bool doDNSandIPStorage(Url uri, sockaddr_in& server, bool doIPValidation) {
    WSADATA wsaData;

    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        printf("WSAStartup error %d\n", WSAGetLastError());
        WSACleanup();
        return false;
    }
    DWORD IP = inet_addr(uri.host.c_str());
    logOutput&& cout << "\tDoing DNS...";

    auto start = chrono::high_resolution_clock::now();
    if (IP == INADDR_NONE)
    {
        auto remote = gethostbyname(uri.host.c_str());
        if (remote == NULL)
        {
            logOutput&& cout << "failed with " << WSAGetLastError() << endl;
            return false;
        }
        else {
            memcpy((char*)&(server.sin_addr), remote->h_addr, remote->h_length);
            auto end = chrono::high_resolution_clock::now();
            auto time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
            mLock.lock();
            D++;
            mLock.unlock();
            logOutput&& cout << "done in " << time_elapsed << " ms, found " << inet_ntoa(server.sin_addr) << endl;
        }
    }
    else
    {
        server.sin_addr.S_un.S_addr = IP;
        auto end = chrono::high_resolution_clock::now();
        auto time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
        logOutput&& cout << "done in " << time_elapsed << " ms, found " << inet_ntoa(server.sin_addr) << endl;
    }

    if (doIPValidation) {
        logOutput&& cout << "\tChecking IP uniqueness... ";
        mLock.lock();
        auto ipCheck = seenIPs.insert(server.sin_addr.S_un.S_addr);

        if (ipCheck.second == true) {
            logOutput&& cout << "passed" << endl;
            I++;
            mLock.unlock();
        }
        else {
            logOutput&& cout << "failed" << endl;
            mLock.unlock();
            return false;
        }
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(uri.port.c_str()));
    return true;
}

void parseBody(string result, string host) {
    auto start = chrono::high_resolution_clock::now();
    logOutput&& cout << "      + Parsing Page... ";
    string headers = result.substr(0, result.find("\r\n\r\n"));
    string body = result.substr(result.find("\r\n\r\n") + 4);

    HTMLParserBase* parser = new HTMLParserBase;
    int nLinks = 0;

    char* body_C = new char[body.size()];
    strcpy_s(body_C, body.size() + 2, body.c_str());

    string hostUri = "http://" + host;
    char* uri_C = new char[hostUri.size() + 2];
    strcpy_s(uri_C, hostUri.size() + 2, hostUri.c_str());

    char* linkBuffer = parser->Parse(body_C, (int)strlen(body_C), uri_C, (int)strlen(uri_C), &nLinks);

    if (nLinks < 0) {
        //printf("Error in parsing\n");
        nLinks = 0;
    }

    auto end = chrono::high_resolution_clock::now();
    auto time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    logOutput&& cout << "done in " << time_elapsed << " ms with " << nLinks << " links" << endl;
    mLock.lock();
    L += nLinks;
    mLock.unlock();
}

void printHeaders(string result) {
    string headers = result.substr(0, result.find("\r\n\r\n"));
    logOutput&& cout << "----------------------------------------" << endl;
    cout << headers << endl;
}

void parseUrlRobotsandConnect(string url) {
    cout << "URL: " << url << endl;
    cout << "\tParsing URL... ";
    Url* uri_ptr = Url::Parse(url);

    if (!uri_ptr) return;

    Url uri = *uri_ptr;
    cout << "host " << uri.host << ", port " << uri.port << ", request " << uri.query << endl;

    if (!checkHostUniqueness(uri.host)) return;

    struct hostent* remote;
    struct sockaddr_in server;

    if (!doDNSandIPStorage(uri, server, true)) return;

    string result = connectToServer(uri, server, "HEAD", "robots", MAX_ROBOTS_SIZE, "/robots.txt");
    if (result.length() == 0) return;
    if (!verifyHeader(result, '4')) return;

    result = connectToServer(uri, server, "GET", "page", MAX_PAGE_SIZE);
    if (result.length() == 0) return;
    if (!verifyHeader(result, '2')) return;

    parseBody(result, uri.host);
    WSACleanup();
}

void parseUrlandConnect(string url) {
    cout << "URL: " << url << endl;
    cout << "\tParsing URL... ";
    Url* uri_ptr = Url::Parse(url);

    if (!uri_ptr) return;

    Url uri = *uri_ptr;
    cout << "host " << uri.host << ", port " << uri.port << ", request " << uri.query << endl;

    struct hostent* remote;
    struct sockaddr_in server;

    if (!doDNSandIPStorage(uri, server, false)) return;

    string result = connectToServer(uri, server, "GET", "page", MAX_PAGE_SIZE);
    if (result.length() == 0) return;
    if (!verifyHeader(result, '2')) return;

    parseBody(result, uri.host);
    printHeaders(result);
    WSACleanup();
}

void printStats() {
    onGoing = true;
    auto t = chrono::steady_clock::now();
    auto nextTime = t + chrono::seconds(2);
    while (onGoing)
    {
        this_thread::sleep_for(chrono::seconds(1));
        printf("[%3d] %4d Q %6d E %7d H %6d D %6d I %5d R %5d C %5d L %4dK\n",
            (int)(chrono::duration_cast<chrono::seconds>(nextTime - t).count()),
            activeThreads,
            urlsToParse.size(),
            E, H, D, I, R, C, L / 1000);
        float timeDiff = (float)(chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t).count());
        printf("\t*** crawling %.1f pps @ %.1f Mbps\n", 
            ((float)pagesSinceLastWake) * 1000 / (timeDiff), 
            ((float)bytesSinceLastWake) * 8.0 / timeDiff / 1000.0);
        nextTime += chrono::seconds(2);
        mLock.lock();
        pagesSinceLastWake = 0;
        bytesSinceLastWake = 0;
        mLock.unlock();
    }
}

void parseUrlRobotsConnectNoOut(string url) {
    Url* uri_ptr = Url::Parse(url);

    if (!uri_ptr) return;

    Url uri = *uri_ptr;

    if (!checkHostUniqueness(uri.host)) return;

    struct hostent* remote;
    struct sockaddr_in server;

    if (!doDNSandIPStorage(uri, server, true)) return;

    string result = connectToServer(uri, server, "HEAD", "robots", MAX_ROBOTS_SIZE, "/robots.txt");
    if (result.length() == 0) return;
    if (!verifyHeader(result, '4')) return;
    mLock.lock();
    R++;
    mLock.unlock();

    result = connectToServer(uri, server, "GET", "page", MAX_PAGE_SIZE);
    if (result.length() == 0) return;
    string headers = result.substr(0, result.find("\r\n\r\n"));
    string statusCode = headers.substr(9, 3);
    mLock.lock();
    switch (statusCode[0]) {
    case '2':
    case '3':
    case '4':
    case '5':
        statusCodeMap[statusCode[0]]++;
        break;
    default:
        statusCodeMap['o']++;
        break;
    }
    C++;
    pagesSinceLastWake++;
    mLock.unlock();
    if (!verifyHeader(result, '2')) return;

    parseBody(result, uri.host);
}

void parseAndCrawl() {
    mLock.lock();
    activeThreads++;
    mLock.unlock();
    while (true) {
        mLock.lock();
        if (urlsToParse.size() == 0) {
            mLock.unlock();
            break;
        }
        string url = urlsToParse.front();
        urlsToParse.pop();
        parsedUrls[url] = false;
        E++;
        mLock.unlock();

        parseUrlRobotsConnectNoOut(url);

    }
    mLock.lock();
    activeThreads--;
    mLock.unlock();
}

void Run(int threads) {
    try {
        auto start = chrono::steady_clock::now();
        vector<thread> threadList;

        thread statsThread(printStats);

        logOutput = false;

        for (int i = 0; i < threads; i++) {
            threadList.emplace_back(parseAndCrawl);
        }

        for (int i = 0; i < threads; i++) {
            threadList[i].join();
        }

        onGoing = false;
        statsThread.join();

        auto end = chrono::steady_clock::now();
        int timeElapsed = chrono::duration_cast<chrono::seconds>(end - start).count();
        printf("Extracted %d URLs @ %d/s\n", E, E / timeElapsed);
        printf("Looked Up %d DNS Names @ %d/s\n", H, H / timeElapsed);
        printf("Attempted %d robots @ %d/s\n", I, I / timeElapsed);
        printf("Crawled %d pages @ %d/s (%.2fMB)\n", C, C / timeElapsed, (float)totalBytes / 1000000.0);
        printf("Parsed %d Links @ %d/s\n", L, L / timeElapsed);
        printf("HTTP codes: 2xx = %d, 3xx = %d, 4xx = %d, 5xx = %d, other = %d\n",
            statusCodeMap['2'],
            statusCodeMap['3'],
            statusCodeMap['4'],
            statusCodeMap['5'],
            statusCodeMap['o']);
    }
    catch (string error) {
        cerr << error << endl;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        cout << "Too few arguments" << endl;
        return 0;
    }

    if (argc > 3) {
        cout << "Too many arguments" << endl;
        return 0;
    }

    if (argc == 2) {
        string url = argv[1];
        parseUrlandConnect(url);
    }
    else {
        string threads = argv[1];
        string filename = argv[2];

        int threadCount = atoi(threads.c_str());

        ifstream file(filename);

        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            streamsize fileSize = file.tellg();
            file.seekg(0, 0);
            cout << "Opened " << filename << " with size " << fileSize << endl;
            string line;
            while (getline(file, line)) {
                urlsToParse.push(line);
            }
            file.close();

            Run(threadCount);
        }
        else {
            cerr << "Error opening file: " << filename << endl;
        }
    }

    return 0;
}
