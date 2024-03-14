//
// Created by ryan on 12/2/23.
//
#include <unistd.h>
#include <iostream>
#include <fcntl.h>
#include <cstdio>
#include <string>

using namespace std;

static constexpr size_t TEST_LIMIT = 1'000'000;
static constexpr const char *TESTPATH = "mnt/testbig";

int main()
{
        int fd = open(TESTPATH, O_CREAT | O_WRONLY,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (fd < 0) {
                perror("open for write");
                return 1;
        }

        for (size_t i = 0; i < TEST_LIMIT; i++){
                string s = to_string(i) + '\n';
                if (write(fd, s.c_str(), s.size()) != s.size()) {
                        perror(("write "s + to_string(i)).c_str());
                        break;
                }
        }

        close(fd);

        fd = open(TESTPATH, O_RDONLY);
        if (fd < 0) {
                perror("open for read");
                return 1;
        }

        for (size_t i = 0; i < TEST_LIMIT; i++) {
                string s;
                s.reserve(256);
                char c = 0;
                while (c != '\n') {
                        if (read(fd, &c, 1) < 0) {
                                perror(("read '" + s + "'").c_str());
                                break;
                        }
                        s += c;
                }
                if (s.back() == '\n')
                        s.pop_back();
                size_t val = atol(s.c_str());
                if (val != i) {
                        cerr << "failed at i=" << i << ", val=" << val << endl;
                        break;
                }
        }
        close(fd);
        cout << "test finished" << endl;

        return 0;
}