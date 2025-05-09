#include <iostream>
#include <pty.h>
#include <cstring>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <termios.h>
#include <bits/termios-tcflow.h>

class CloseAfterUse {
private:
    int fd;
public:
    CloseAfterUse(int fd) : fd(fd) {}
    ~CloseAfterUse() {
        close(fd);
    }
    CloseAfterUse() = delete;
    CloseAfterUse(const CloseAfterUse&) = delete;
    CloseAfterUse& operator=(const CloseAfterUse&) = delete;
    CloseAfterUse(CloseAfterUse&&) = delete;
    CloseAfterUse& operator=(CloseAfterUse&&) = delete;
};

template <typename T> constexpr bool bitcmp(const T &c1, const T &c2) {
    const char *b1 = reinterpret_cast<const char *>(&c1);
    const char *b2 = reinterpret_cast<const char *>(&c2);
    for (int i = 0; i < sizeof(T); ++i) {
        if (b1[i] != b2[i]) {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <dev>\n";
        return 1;
    }
    int phystty, masterfd, slavefd;
    phystty = open(argv[1], O_RDWR | O_NOCTTY | O_SYNC);
    if (phystty < 0) {
        std::cerr << "Failed open\n";
        return 1;
    }
    CloseAfterUse physttyCloser{phystty};
    std::string name;
    {
        name.resize(512);
        openpty(&masterfd, &slavefd, name.data(), NULL, NULL);
        name.resize(strlen(name.data()));
    }
    CloseAfterUse masterfdCloser{masterfd};
    CloseAfterUse slavefdCloser{slavefd};
    std::cout << "Tty: " << name << std::endl;
    int ep = epoll_create(10);
    if (ep < 0) {
        std::cerr << "Failed epoll\n";
        return 1;
    }
    CloseAfterUse epCloser{ep};
    struct epoll_event pev {.events = EPOLLIN | EPOLLPRI, .data = {.fd = phystty}};
    struct epoll_event ev {.events = EPOLLIN | EPOLLPRI, .data = {.fd = masterfd}};
    if (epoll_ctl(ep, EPOLL_CTL_ADD, phystty, &pev)) {
        std::cerr << "Failed epoll_ctl\n";
        return 1;
    }
    if (epoll_ctl(ep, EPOLL_CTL_ADD, masterfd, &ev)) {
        std::cerr << "Failed epoll_ctl\n";
        return 1;
    }
    struct termios ttysettings{};
    if (tcgetattr(masterfd, &ttysettings) != 0) {
        std::cerr << "Failed tcgetattr\n";
        return 1;
    }
    tcsetattr(phystty, TCSANOW, &ttysettings);
    while (true) {
        constexpr int max_events = 10;
        struct epoll_event events[max_events];
        int n = epoll_wait(ep, events, max_events, -1);
        if (n < 0) {
            std::cerr << "Failed epoll_wait\n";
            return 1;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == phystty) {
                char buf[1024];
                auto restore = fcntl(phystty, F_GETFL);
                fcntl(phystty, F_SETFL, restore | O_NONBLOCK);
                int r = read(phystty, buf, sizeof(buf));
                if (r < 0 && errno == EAGAIN) {
                    r = 0;
                }
                fcntl(phystty, F_SETFL, restore);
                if (r < 0) {
                    std::cout <<  "Failed read\n";
                    return 1;
                }
                for (int i = 0, s = 0; i < r; ++i) {
                    if (buf[i] == '\r') {
                        std::string line{};
                        line.append(buf + s, i - s);
                        std::cout << "bin> ";
                        for (int i = 0; i < line.size(); i++) {
                            /*if ((line[i] < '0' || line[i] > '9') && (line[i] < 'a' || line[i] > 'z') || (line[i] < 'A' || line[i] > 'Z') && line[i] != ' ' && line[i] != '.' && line[i] != ',' && line[i] != ':' && line[i] != ';' && line[i] != '\'' && line[i] != '"' && line[i] != '#') {
                                line[i] = '?';
                            }*/
                            unsigned char c = line[i];
                            unsigned char c1 = c >> 4;
                            unsigned char c2 = c & 0x0F;
                            char ch[3];
                            ch[2] = '\0';
                            if (c1 < 10) {
                                ch[0] = ('0' + c1);
                            } else {
                                ch[0] = ('A' + c1 - 10);
                            }
                            if (c2 < 10) {
                                ch[1] = ('0' + c2);
                            } else {
                                ch[1] = ('A' + c2 - 10);
                            }
                            std::cout << " " << ch;
                        }
                        std::cout << "\n";
                        //std::cout << "> " << line << "\n";
                        s = i + 1;
                    }
                }
                if (write(masterfd, buf, r) < 0) {
                    std::cout <<  "Failed write\n";
                    return 1;
                }
            }
            if (events[i].data.fd == masterfd) {
                struct termios ttysettingsNew{};
                if (tcgetattr(masterfd, &ttysettingsNew) != 0) {
                    std::cerr << "Failed tcgetattr\n";
                    return 1;
                }
                if (!bitcmp(ttysettings, ttysettingsNew)) {
                    tcsetattr(phystty, TCSANOW, &ttysettingsNew);
                    ttysettings = ttysettingsNew;
                    std::cout << "New ttysettings\n";
                }

                if (events[i].events & EPOLLIN) {
                    char buf[1024];
                    auto restore = fcntl(masterfd, F_GETFL);
                    fcntl(masterfd, F_SETFL, restore | O_NONBLOCK);
                    int r = read(masterfd, buf, sizeof(buf));
                    if (r < 0 && errno == EAGAIN) {
                        r = 0;
                    }
                    fcntl(masterfd, F_SETFL, restore);
                    if (r < 0) {
                        std::cout <<  "Failed read\n";
                        return 1;
                    }
                    for (int i = 0, s = 0; i < r; ++i) {
                        if (buf[i] == '\r') {
                            std::string line{};
                            line.append(buf + s, i - s);
                            std::cout << "< " << line << "\n";
                            s = i + 1;
                        }
                    }
                    if (write(phystty, buf, r) < 0) {
                        std::cout <<  "Failed write\n";
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}
