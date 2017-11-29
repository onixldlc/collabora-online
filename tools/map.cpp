/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "config.h"

#include <vector>
#include <iostream>

#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <locale.h>

#include <Util.hpp>

typedef unsigned long long addr_t;

bool DumpHex = false;

#define MAP_SIZE 20
#define PATH_SIZE 1000 // No harm in having it much larger than strictly necessary. Avoids compiler warning.
#define BUFFER_SIZE 9600

static int read_buffer(char *buffer, unsigned size,
                       const char *file, char sep)
{
    int file_desc;
    unsigned total_bytes = 0;

    file_desc = open(file, O_RDONLY);
    if (file_desc == -1)
        return 0;

    for (;;)
    {
        ssize_t number_bytes = read(file_desc,
                                    buffer + total_bytes,
                                    size - total_bytes);
        if (number_bytes == -1)
        {
            if (errno==EINTR)
                continue;
            break;
        }

        total_bytes += number_bytes;
        if (total_bytes == size)
        {
            --total_bytes;
            break;
        }

        if (number_bytes==0)
            break;  // EOF
    }

    close(file_desc);

    if (total_bytes)
    {
        int i=total_bytes;

        while (i--)
            if (buffer[i]=='\n' || buffer[i]=='\0')
                buffer[i]=sep;

        if (buffer[total_bytes-1]==' ')
            buffer[total_bytes-1]='\0';
    }

    buffer[total_bytes] = '\0';
    return total_bytes;
}

static void dumpPages(unsigned proc_id, const char *type, const std::vector<addr_t> &pages)
{
    char path_proc[PATH_SIZE];
    snprintf(path_proc, sizeof(path_proc), "/proc/%d/mem", proc_id);
    int mem_fd = open(path_proc, 0);
    if (mem_fd < 0)
        error(EXIT_FAILURE, errno, "Failed to open %s", path_proc);

    size_t cnt = 0;
    for (auto page : pages)
    {
        printf ("%s page: 0x%.8llx (%d/%d)\n",
                type, page, (int)++cnt, (int)pages.size());

        std::vector<char> pageData;
        pageData.resize(0x1000);

        if (lseek(mem_fd, page, SEEK_SET) < 0)
            error(EXIT_FAILURE, errno, "Failed to seek in /proc/<pid>/mem to %lld", page);
        if (read(mem_fd, &pageData[0], 0x1000) != 0x1000)
            error(EXIT_FAILURE, errno, "Failed to read page %lld from /proc/<pid>/mem", page);
        Util::dumpHex(std::cout, "", "", pageData);
    }

    close (mem_fd);
}

static std::vector<char> compressBitmap(const std::vector<char> &bitmap)
{
    size_t i;
    std::vector<char> output;
    for (i = 0; i < bitmap.size(); ++i)
    {
        char cur;
        int cnt = 0;
        size_t j = i;
        for (cur = bitmap[j]; bitmap[j] == cur; ++j)
            ++cnt;
        output.push_back(cur);
        if (cnt > 3)
        {
            char num[16];
            output.push_back('[');
            sprintf(num, "%d", cnt);
            for (int cpy = 0; num[cpy] != '\0'; ++cpy)
                output.push_back(num[cpy]);
            output.push_back(']');
            i += cnt - 1;
        }
    }

    output.push_back('\0');
    return output;
}

static void dump_unshared(unsigned proc_id, const char *type, const std::vector<addr_t> &vaddrs)
{
    char path_proc[PATH_SIZE];
    snprintf(path_proc, sizeof(path_proc), "/proc/%d/pagemap", proc_id);
    int fd = open(path_proc, 0);
    if (fd < 0)
        error(EXIT_FAILURE, errno, "Failed to open %s", path_proc);

    std::vector<char> bitmap;
    std::vector<addr_t> vunshared;
    addr_t numShared = 0, numOwn = 0;
    for (auto p : vaddrs)
    {
        if (lseek(fd, (p / 0x1000 * 8), SEEK_SET) < 0)
            error(EXIT_FAILURE, errno, "Failed to seek in pagemap");
        addr_t vaddrData;
        if (read(fd, &vaddrData, 8) < 0)
            error(EXIT_FAILURE, errno, "Failed to read vaddrdata");
        {
            // https://patchwork.kernel.org/patch/6787921/
//            fprintf(stderr, "addr: 0x%8llx bits: 0x%8llx : %s\n", p, vaddrData,
//                    (vaddrData & ((addr_t)1 << 56)) ? "unshared" : "shared");
            if (vaddrData & ((addr_t)1 << 56))
            {
                numOwn++;
                bitmap.push_back('*');
                vunshared.push_back(p);
            }
            else
            {
                numShared++;
                bitmap.push_back('.');
            }
        }
    }
    close (fd);

    printf ("Totals for %s\n", type);
    printf ("\tshared   %5lld (%lldkB)\n", numShared, numShared * 4);
    printf ("\tunshared %5lld (%lldkB)\n", numOwn, numOwn * 4);

    std::vector<char> compressed = compressBitmap(bitmap);
    printf ("RLE sharing bitmap:\n%s\n\n", &compressed[0]);

    if (DumpHex)
    {
        printf ("Un-shared data dump\n");
        dumpPages(proc_id, type, vunshared);
    }
}

static void total_smaps(unsigned proc_id, const char *file, const char *cmdline)
{
    FILE *file_pointer;
    char buffer[BUFFER_SIZE];

    addr_t total_private_dirty = 0ull;
    addr_t total_private_clean = 0ull;
    addr_t total_shared_dirty = 0ull;
    addr_t total_shared_clean = 0ull;
    addr_t smap_value;
    char smap_key[MAP_SIZE];

    std::vector<addr_t> heapVAddrs, anonVAddrs, fileVAddrs;
    std::vector<addr_t> *pushTo = nullptr;

    if ((file_pointer = fopen(file, "r")) == nullptr)
        error(EXIT_FAILURE, errno, "%s", file);

    while (fgets(buffer, sizeof(buffer), file_pointer))
    {
        // collect heap page details
        if (strstr(buffer, "[heap]"))
            pushTo = &heapVAddrs;
        else if (strstr(buffer, "/"))
            pushTo = &fileVAddrs;
        else
            pushTo = &anonVAddrs;

        if (strstr(buffer, " rw-p "))
        {
            addr_t start, end;
            // 012d0000-0372f000 rw-p 00000000 00:00 0  [heap]
            if (sscanf(buffer, "%llx-%llx rw-p", &start, &end) == 2)
            {
                for (addr_t p = start; p < end; p += 0x1000)
                    pushTo->push_back(p);
            }
            else
                fprintf (stderr, "malformed heap line '%s'\n", buffer);
        }

        if (buffer[0] >= 'A' && buffer[0] <= 'Z')
        {
            if (sscanf(buffer, "%20[^:]: %llu", smap_key, &smap_value) == 2)
            {
                if (strncmp("Shared_Dirty", smap_key, 12) == 0)
                {
                    total_shared_dirty += smap_value;
                    continue;
                }
                if (strncmp("Shared_Clean", smap_key, 12) == 0)
                {
                    total_shared_clean += smap_value;
                    continue;
                }
                if (strncmp("Private_Dirty", smap_key, 13) == 0)
                {
                    total_private_dirty += smap_value;
                    continue;
                }
                if (strncmp("Private_Clean", smap_key, 13) == 0)
                {
                    total_private_clean += smap_value;
                    continue;
                }
            }
        }
    }

    printf("%s\n", cmdline);
    printf("Process ID    :%20d\n", proc_id);
    printf("--------------------------------------\n");
    printf("Shared Clean  :%20lld kB\n", total_shared_clean);
    printf("Shared Dirty  :%20lld kB\n", total_shared_dirty);
    printf("Private Clean :%20lld kB\n", total_private_clean);
    printf("Private Dirty :%20lld kB\n", total_private_dirty);
    printf("--------------------------------------\n");
    printf("Shared        :%20lld kB\n", total_shared_clean + total_shared_dirty);
    printf("Private       :%20lld kB\n", total_private_clean + total_private_dirty);
    printf("--------------------------------------\n");
    printf("Heap page cnt :%20lld\n", (addr_t)heapVAddrs.size());
    printf("Anon page cnt :%20lld\n", (addr_t)anonVAddrs.size());
    printf("File page cnt :%20lld\n", (addr_t)fileVAddrs.size());
    printf("\n");
    dump_unshared(proc_id, "heap", heapVAddrs);
    dump_unshared(proc_id, "anon", anonVAddrs);
    dump_unshared(proc_id, "file", fileVAddrs);
}

int main(int argc, char **argv)
{
    DIR *root_proc;
    struct dirent *dir_proc;

    char path_proc[PATH_SIZE];
    char cmdline[BUFFER_SIZE];

    bool help = false;
    unsigned forPid = 0;
    const char *appOrPid = nullptr;

    setlocale (LC_ALL, "");
    getopt(argc, argv, "");

    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (strstr(arg, "--help"))
            help = true;
        else if (strstr(arg, "--hex"))
            DumpHex = true;
        else
            appOrPid = arg;
    }
    if (appOrPid == NULL && forPid == 0)
        help = true;

    if (help)
    {
        fprintf(stderr, "Usage: loolmap --hex <name of process|pid>\n");
        fprintf(stderr, "Dump memory map information for a given process\n");
        return 0;
    }

    forPid = atoi(appOrPid);

    root_proc = opendir("/proc");
    if (!root_proc)
        error(EXIT_FAILURE, errno, "%s", "/proc");

    while ((dir_proc = readdir(root_proc)))
    {
        if (!dir_proc && !dir_proc->d_name[0])
            error(EXIT_FAILURE, ENOTDIR, "bad dir");

        if (*dir_proc->d_name > '0' && *dir_proc->d_name <= '9')
        {
            unsigned pid_proc = strtoul(dir_proc->d_name, nullptr, 10);
            snprintf(path_proc, sizeof(path_proc), "/proc/%s/%s", dir_proc->d_name, "cmdline");
            if (read_buffer(cmdline, sizeof(cmdline), path_proc, ' ') &&
                (forPid == pid_proc || (forPid == 0 && strstr(cmdline, appOrPid) && !strstr(cmdline, argv[0]))))
            {
                snprintf(path_proc, sizeof(path_proc), "/proc/%s/%s", dir_proc->d_name, "smaps");
                total_smaps(pid_proc, path_proc, cmdline);
            }
        }
    }

    return EXIT_SUCCESS;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
