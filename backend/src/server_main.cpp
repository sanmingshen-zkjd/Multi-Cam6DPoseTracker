#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

struct AppState {
  std::mutex m;
  std::vector<std::string> sources;
  bool playing = false;
  bool sync = false;
  int64_t frame = 0;
};

static void closeSock(socket_t s) {
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
}

static std::string jsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    if (c == '\\' || c == '"') { out.push_back('\\'); out.push_back(c); }
    else if (c == '\n') out += "\\n";
    else out.push_back(c);
  }
  return out;
}

static std::map<std::string, std::string> parseQuery(const std::string& q) {
  std::map<std::string, std::string> kv;
  std::stringstream ss(q);
  std::string token;
  while (std::getline(ss, token, '&')) {
    auto pos = token.find('=');
    if (pos == std::string::npos) continue;
    kv[token.substr(0, pos)] = token.substr(pos + 1);
  }
  return kv;
}

static std::string buildStateJson(AppState& st) {
  std::lock_guard<std::mutex> lk(st.m);
  std::ostringstream os;
  os << "{\"playing\":" << (st.playing ? "true" : "false")
     << ",\"sync\":" << (st.sync ? "true" : "false")
     << ",\"frame\":" << st.frame
     << ",\"sources\":[";
  for (size_t i = 0; i < st.sources.size(); ++i) {
    if (i) os << ',';
    os << '"' << jsonEscape(st.sources[i]) << '"';
  }
  os << "]}";
  return os.str();
}

static void writeHttp(socket_t c, int code, const std::string& body, const char* ctype = "application/json") {
  const char* reason = (code == 200 ? "OK" : (code == 404 ? "Not Found" : "Bad Request"));
  std::ostringstream os;
  os << "HTTP/1.1 " << code << ' ' << reason << "\r\n"
     << "Content-Type: " << ctype << "\r\n"
     << "Access-Control-Allow-Origin: *\r\n"
     << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
     << "Access-Control-Allow-Headers: Content-Type\r\n"
     << "Content-Length: " << body.size() << "\r\n\r\n"
     << body;
  const std::string resp = os.str();
  send(c, resp.data(), (int)resp.size(), 0);
}

static void handleClient(socket_t c, AppState& st) {
  char buf[8192];
  int n = recv(c, buf, sizeof(buf) - 1, 0);
  if (n <= 0) { closeSock(c); return; }
  buf[n] = '\0';
  std::string req(buf);

  std::istringstream is(req);
  std::string method, path, version;
  is >> method >> path >> version;

  if (method == "OPTIONS") { writeHttp(c, 200, "{}"); closeSock(c); return; }

  std::string route = path;
  std::string query;
  auto qPos = path.find('?');
  if (qPos != std::string::npos) {
    route = path.substr(0, qPos);
    query = path.substr(qPos + 1);
  }
  auto q = parseQuery(query);

  if (method == "GET" && route == "/api/health") {
    writeHttp(c, 200, "{\"ok\":true,\"service\":\"multicam_pose_backend\"}");
  } else if (method == "GET" && route == "/api/state") {
    writeHttp(c, 200, buildStateJson(st));
  } else if (method == "POST" && route == "/api/sources/add") {
    auto it = q.find("path");
    if (it == q.end() || it->second.empty()) writeHttp(c, 400, "{\"error\":\"missing path\"}");
    else {
      std::lock_guard<std::mutex> lk(st.m);
      st.sources.push_back(it->second);
      writeHttp(c, 200, "{\"ok\":true}");
    }
  } else if (method == "POST" && route == "/api/sources/remove_last") {
    std::lock_guard<std::mutex> lk(st.m);
    if (!st.sources.empty()) st.sources.pop_back();
    writeHttp(c, 200, "{\"ok\":true}");
  } else if (method == "POST" && route == "/api/play") {
    std::lock_guard<std::mutex> lk(st.m);
    st.playing = true;
    st.sync = (q["sync"] == "1" || q["sync"] == "true");
    writeHttp(c, 200, "{\"ok\":true}");
  } else if (method == "POST" && route == "/api/pause") {
    std::lock_guard<std::mutex> lk(st.m);
    st.playing = false;
    writeHttp(c, 200, "{\"ok\":true}");
  } else if (method == "POST" && route == "/api/step") {
    int delta = 1;
    if (q.count("delta")) delta = std::stoi(q["delta"]);
    std::lock_guard<std::mutex> lk(st.m);
    st.frame = std::max<int64_t>(0, st.frame + delta);
    writeHttp(c, 200, "{\"ok\":true}");
  } else {
    writeHttp(c, 404, "{\"error\":\"not found\"}");
  }

  closeSock(c);
}

int main(int argc, char** argv) {
  int port = 18080;
  if (argc > 1) port = std::max(1, std::atoi(argv[1]));

#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }
#endif

  socket_t server = socket(AF_INET, SOCK_STREAM, 0);
  if (server == kInvalidSocket) {
    std::cerr << "socket create failed\n";
    return 1;
  }

  int opt = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);

  if (bind(server, (sockaddr*)&addr, sizeof(addr)) < 0) {
    std::cerr << "bind failed\n";
    closeSock(server);
    return 1;
  }
  if (listen(server, 16) < 0) {
    std::cerr << "listen failed\n";
    closeSock(server);
    return 1;
  }

  std::cout << "multicam_pose_backend listening on http://127.0.0.1:" << port << "\n";

  AppState state;
  while (true) {
    sockaddr_in caddr{};
#ifdef _WIN32
    int len = sizeof(caddr);
#else
    socklen_t len = sizeof(caddr);
#endif
    socket_t client = accept(server, (sockaddr*)&caddr, &len);
    if (client == kInvalidSocket) continue;
    std::thread([client, &state]() { handleClient(client, state); }).detach();
  }

  closeSock(server);
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
