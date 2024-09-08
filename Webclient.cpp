/*
    Name: Wahib Sabir Kapdi
    Class: CSCE 612
    Semester: Graduate Student - MCS - Fall 2024 - 1st Semester
*/

#include "pch.h"
#pragma comment(lib, "ws2_32.lib")

using namespace std;

const int INITIAL_BUFFER_SIZE = 4096;
const int MAX_ROBOTS_SIZE = 16e3;
const int MAX_PAGE_SIZE = 2e9;
const string USER_AGENT = "wahibsCrawler/1.1";

set<string> seenHosts;
set<DWORD> seenIPs;

class Url {
public:
    string path = "/", scheme = "http", host, port="80", query, fragment;

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

    static Url* Parse (string url) {
        Url result;

        if (url.length() == 0) {
            return new Url();
        }

        string scheme = url.substr(0, 4);
        if (scheme.compare("http") != 0) {
            cout << "Parsing URL... failed with invalid scheme" << endl;
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
            cout << "Parsing URL... failed with invalid port" << endl;
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

string connectToServer(Url uri, sockaddr_in& server, string httpMethod, string connectingOn, int maxSize, string path="") {
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(uri.port.c_str()));
    WSADATA wsaData;

    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        printf("WSAStartup error %d\n", WSAGetLastError());
        WSACleanup();
        return "";
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        printf("socket() generated error %d\n", WSAGetLastError());
        WSACleanup();
        return "";
    }

    if (path.size() == 0) {
        cout << "      * Connecting on " << connectingOn << "... ";
    }
    else {
        cout << "\tConnecting on " << connectingOn << "... ";
    }

    auto start = chrono::high_resolution_clock::now();

    if (connect(sock, (struct sockaddr*)&server, sizeof(struct sockaddr_in)) == SOCKET_ERROR)
    {
        cout << "failed with " << WSAGetLastError () << endl;
        closesocket(sock);
        WSACleanup();
        return "";
    }

    auto end = chrono::high_resolution_clock::now();
    auto time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    cout << "done in " << time_elapsed << " ms" << endl;

    start = chrono::high_resolution_clock::now();
    cout << "\tLoading... ";

    char* recvBuffer = new char[INITIAL_BUFFER_SIZE];
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

    string part1 = path.size() > 0?path:uri.path;

    if (path.size() == 0 && uri.query.size() > 0) {
        part1 += "?" + uri.query;
    }

    string request = ((string)(httpMethod + " " + part1 + " HTTP/1.0\r\nHost: " + uri.host + "\r\nUser-agent: " + USER_AGENT + "\r\nConnection: close\r\n\r\n")).c_str();

    if (send(sock, request.c_str(), request.size(), 0) == SOCKET_ERROR) {
        cout << "failed with " << WSAGetLastError() << endl;
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
            if (time_elapsed > 10000) {
                cout << "failed with slow download" << endl;
                recvBuffer[curPos] = '\0';
                closesocket(sock);
                WSACleanup();
                return "";
            }
            if (bytes == SOCKET_ERROR) {
                cout << "failed with " << WSAGetLastError() << " on recv" << endl;
                return "";
            }

            if (curPos == 0) {
                if (bytes < 6) {
                    cout << "failed with non-HTTP header (does not begin with HTTP/)" << endl;
                    return "";
                }
                string headerStart = ((string)recvBuffer).substr(0, 5);

                if (headerStart.compare("HTTP/") != 0) {
                    cout << "failed with non-HTTP header (does not begin with HTTP/)" << endl;
                    return "";
                }
            }

            curPos += bytes;

            if (bytes == 0) {
                recvBuffer[curPos] = '\0';
                break;
            }
            if (curPos >= maxSize) {
                cout << "failed with exceeding max" << endl;
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
            cout << "failed with " << WSAGetLastError() << endl;
            closesocket(sock);
            WSACleanup();
            return "";
        }
        else
        {
            cout << "failed with timeout" << endl;
            closesocket(sock);
            WSACleanup();
            return "";
        }
    }

    closesocket(sock);

    end = chrono::high_resolution_clock::now();
    time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
    cout << "done in " << time_elapsed << " ms with " << curPos << " bytes" << endl;

    string result = string(recvBuffer, recvBuffer + curPos);

    return result;
}

bool verifyHeader(string result, char acceptanceCode) {
    string headers = result.substr(0, result.find("\r\n\r\n"));
    string statusCode = headers.substr(9, 3);
    cout << "\tVerifying header... status code " << statusCode << endl;
    if (statusCode.size() == 3 && statusCode[0] == acceptanceCode) {
        return true;
    }

    return false;
}

bool checkHostUniqueness(string host) {
    cout << "\tChecking host uniqueness... ";
    auto hostCheck = seenHosts.insert(host);

    if (hostCheck.second == true) {
        cout << "passed" << endl;
        return true;
    }
    else {
        cout << "failed" << endl;
        return false;
    }
}

bool doDNSandIPStorage(Url uri, sockaddr_in &server, bool doIPValidation) {
    WSADATA wsaData;

    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        printf("WSAStartup error %d\n", WSAGetLastError());
        WSACleanup();
        return false;
    }
    DWORD IP = inet_addr(uri.host.c_str());
    cout << "\tDoing DNS...";

    auto start = chrono::high_resolution_clock::now();
    if (IP == INADDR_NONE)
    {
        auto remote = gethostbyname(uri.host.c_str());
        if (remote == NULL)
        {
            cout << "failed with " << WSAGetLastError() << endl;
            return false;
        }
        else {
            memcpy((char*)&(server.sin_addr), remote->h_addr, remote->h_length);
            auto end = chrono::high_resolution_clock::now();
            auto time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
            cout << "done in " << time_elapsed << " ms, found " << inet_ntoa(server.sin_addr) << endl;
        }
    }
    else
    {
        server.sin_addr.S_un.S_addr = IP;
        auto end = chrono::high_resolution_clock::now();
        auto time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();
        cout << "done in " << time_elapsed << " ms, found " << inet_ntoa(server.sin_addr) << endl;
    }

    if (doIPValidation) {
        cout << "\tChecking IP uniqueness... ";
        auto ipCheck = seenIPs.insert(server.sin_addr.S_un.S_addr);

        if (ipCheck.second == true) {
            cout << "passed" << endl;
        }
        else {
            cout << "failed" << endl;
            return false;
        }
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(uri.port.c_str()));
    return true;
}

void parseBodyandPrintHeaders(string result, string host) {
    auto start = chrono::high_resolution_clock::now();
    cout << "      + Parsing Page... ";
    string headers = result.substr(0, result.find("\r\n\r\n"));
    string body = result.substr(result.find("\r\n\r\n") + 4);

    //cout << "\nBody Start Here : " << body << endl;

    HTMLParserBase* parser = new HTMLParserBase;
    int nLinks = 0;

    char* body_C = new char[body.size()];
    strcpy_s(body_C, body.size() + 2, body.c_str());

    string hostUri = "http://" + host;
    char* uri_C = new char[hostUri.size() + 2];
    strcpy_s(uri_C, hostUri.size() + 2, hostUri.c_str());

    char* linkBuffer = parser->Parse(body_C, (int)strlen(body_C), uri_C, (int)strlen(uri_C), &nLinks);

    if (nLinks < 0) {
        nLinks = 0;
    }

    auto end = chrono::high_resolution_clock::now();
    auto time_elapsed = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    cout << "done in " << time_elapsed << " ms with " << nLinks << " links" << endl;
}

void printHeaders(string result) {
    string headers = result.substr(0, result.find("\r\n\r\n"));
    cout << "----------------------------------------" << endl;
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

    if(!doDNSandIPStorage(uri, server, true)) return;

    string result = connectToServer(uri, server, "HEAD", "robots", MAX_ROBOTS_SIZE, "/robots.txt");
    if (result.length() == 0) return;
    if (!verifyHeader(result, '4')) return;

    result = connectToServer(uri, server, "GET", "page", MAX_PAGE_SIZE);
    if (result.length() == 0) return;
    if (!verifyHeader(result, '2')) return;

    parseBodyandPrintHeaders(result, uri.host);
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

    parseBodyandPrintHeaders(result, uri.host);
    printHeaders(result);
    WSACleanup();
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
    } else {
        string threads = argv[1];
        string filename = argv[2];

        if (threads.compare("1") != 0) {
            cout << "More than one thread not allowed" << endl;
            return 0;
        }

        ifstream file(filename);

        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            streamsize fileSize = file.tellg();
            file.seekg(0, 0);
            cout << "Opened " << filename << " with size " << fileSize << endl;
            string line;
            while (getline(file, line)) {
                parseUrlRobotsandConnect(line);
            }
            file.close();
        }
        else {
            cerr << "Error opening file: " << filename << endl;
        }
    }


    return 0;
}
