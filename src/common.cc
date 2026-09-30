#include <stdio.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windns.h>
#include <iphlpapi.h>
#include <windows.h>
#include "common.hh"
#include <format>

/*
 *  NOTE: Windows made a lot of dumb decisions about how integer types should be sized
 *        hence, in this file the class WORD, DWORD, QWORD, etc are used to pass references
 *        to shell functions, they are then converted to sane people types afterwards
 */

unsigned long readPipe(void* _args);

string shell_error_to_string(s32 error_code) {
    switch (error_code) {
        case SUCCESS:
            return string("No error");
        case PIPE_CREATION_ERROR:
            return string("Failed to create pipe");
        case PROCESS_CREATION_ERROR:
            return string("Failed to create a sub-process");
        case PIPE_ATTR_ERROR:
            return string("Failed to set attributes on a pipe");
        case EXIT_CODE_READ_FAILURE:
            return string("Failed to read exit code of a sub-process");
        case READ_PIPE_ERROR:
            return string("Failed to read from a pipe");
        case PROCESS_TIMEOUT:
            return string("Process timeout before it could finish");
        case PROCESS_WAIT_ERROR:
            return string("Failed to wait for process");
        default:
            return string("Undefined Shell Error");
    }
}

struct readPipeParams {
    HANDLE hPipe;
    char *buf;
    s32 bufsize;
    string output;
};

s32 shell_exec(string program, strvec args, ShellOutput& output, s32 timeout_secs) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES child_stdout_sa;
    SECURITY_ATTRIBUTES child_stderr_sa;
    HANDLE child_stdout_rd = NULL;
    HANDLE child_stdout_wr = NULL;
    HANDLE child_stderr_rd = NULL;
    HANDLE child_stderr_wr = NULL;
    HANDLE child_stdin = NULL;
    HANDLE stdout_thread = NULL;
    HANDLE stderr_thread = NULL;
    s32 err = SUCCESS;
    auto timeout = timeout_secs == -1 ? INFINITE : timeout_secs * 1000;
    auto command = program;
    DWORD return_code = 0;
    DWORD wait_result = 0;
    auto bufsize = KB(4);
    char* stdout_buf = (char*)malloc(bufsize);
    char* stderr_buf = (char*)malloc(bufsize);
    struct readPipeParams stdout_thread_params = {.hPipe = NULL, .buf = stdout_buf, .bufsize = bufsize, .output = ""};
    struct readPipeParams stderr_thread_params = {.hPipe = NULL, .buf = stderr_buf, .bufsize = bufsize, .output = ""};

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&child_stdout_sa, sizeof(child_stdout_sa));
    ZeroMemory(&child_stderr_sa, sizeof(child_stderr_sa));

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;

    child_stdout_sa.nLength = sizeof(child_stdout_sa);
    child_stdout_sa.bInheritHandle = TRUE;
    child_stderr_sa.nLength = sizeof(child_stdout_sa);
    child_stderr_sa.bInheritHandle = TRUE;

    if(!CreatePipe(&child_stdout_rd, &child_stdout_wr, &child_stdout_sa, 0)
            || !CreatePipe(&child_stderr_rd, &child_stderr_wr, &child_stderr_sa, 0)) {
        err = PIPE_CREATION_ERROR;
        goto end;
    }

    if(!SetHandleInformation(child_stdout_rd, HANDLE_FLAG_INHERIT, 0)
            || !SetHandleInformation(child_stderr_rd, HANDLE_FLAG_INHERIT, 0)) {
        err = PIPE_ATTR_ERROR;
        goto end;
    }

    stdout_thread_params.hPipe = child_stdout_rd;
    stderr_thread_params.hPipe = child_stderr_rd;

    /* Windows equivalent of /dev/null, sends EOF to stdin right away */
    child_stdin = CreateFileA(
            "NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

    /* Set the child stdout / stderr write ends */
    si.hStdInput = child_stdin;
    si.hStdOutput = child_stdout_wr;
    si.hStdError = child_stderr_wr;

    for(auto i = 0; i < args.size(); i++)
        command += " " + args[i];

    if(!CreateProcessA(
                NULL,   /* Use command line, not module name */
                command.data(),
                NULL,   /* Process handle not inheritable */
                NULL,   /* Thread handle not inheritable */
                TRUE,   /* Set handle inheritance */
                0,      /* No creation flags */
                NULL,   /* Use parent's environment block */
                NULL,   /* Use parent's startup directory */
                &si,
                &pi)) {
        err = PROCESS_CREATION_ERROR;
        goto end;
    }
    SafeCloseHandle(child_stdout_wr);
    SafeCloseHandle(child_stderr_wr);
    CloseHandle(child_stdin);
    stdout_thread = CreateThread(NULL, 0, readPipe, (void*)&stdout_thread_params, 0, NULL);
    stderr_thread = CreateThread(NULL, 0, readPipe, (void*)&stderr_thread_params, 0, NULL);
    wait_result = WaitForSingleObject(pi.hProcess, timeout);
    if(wait_result == WAIT_TIMEOUT) {
        err = PROCESS_TIMEOUT;

        TerminateProcess(pi.hProcess, ERROR_TIMEOUT);
        WaitForSingleObject(pi.hProcess, INFINITE);
    } else if (wait_result == WAIT_FAILED) {
        err = PROCESS_WAIT_ERROR;
        goto end;
    }

    WaitForSingleObject(stdout_thread, INFINITE);
    WaitForSingleObject(stderr_thread, INFINITE);
    output.std_out = stdout_thread_params.output;
    stdout_thread_params.hPipe = NULL;
    output.std_err = stderr_thread_params.output;
    stderr_thread_params.hPipe = NULL;

    /* Workaround for some dumb compiler errors... thanks windows */
    if(!GetExitCodeProcess(pi.hProcess, &return_code)) {
        err = EXIT_CODE_READ_FAILURE;
        goto end;
    }
    output.return_code = return_code;

end:
    SafeFree(stdout_buf);
    SafeFree(stderr_buf);

    SafeCloseHandle(child_stdout_rd);
    SafeCloseHandle(child_stdout_wr);
    SafeCloseHandle(child_stderr_rd);
    SafeCloseHandle(child_stderr_wr);
    SafeCloseHandle(child_stdin);
    SafeCloseHandle(stdout_thread);
    SafeCloseHandle(stderr_thread);

    SafeCloseHandle(pi.hProcess);
    SafeCloseHandle(pi.hThread);

    return err;
}

unsigned long readPipe(void* _args) {
    struct readPipeParams* args = (struct readPipeParams*)_args;
    auto hPipe = args->hPipe;
    auto buf = args->buf;
    auto bufsize = args->bufsize;
    DWORD num_read = 0;
    string output = "";
    for (;;) {
        BOOL success = ReadFile(hPipe, buf, bufsize - 1, &num_read, NULL);
        if(!success) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE)
                break;

            return READ_PIPE_ERROR;
        }

        if(num_read == 0) break;

        buf[num_read] = 0;
        output += buf;
    }
    args->output = output;
    return SUCCESS;
}

/*
 *
 * IP Parsing Utils
 *
 */

IP parse_ip(string ip_str) {
    NET_ADDRESS_INFO addr_info = {};
    USHORT port = 0;
    BYTE prefix_length = 0;
    std::wstring ip_wstr = std::wstring(ip_str.begin(), ip_str.end());
    IP out {};

    DWORD result = ParseNetworkString(
            ip_wstr.c_str(),
            NET_STRING_IPV4_ADDRESS
                | NET_STRING_IPV4_SERVICE
                | NET_STRING_IPV4_NETWORK
                | NET_STRING_IPV6_ADDRESS
                | NET_STRING_IPV6_SERVICE
                | NET_STRING_IPV6_NETWORK,
            &addr_info,
            &port,
            &prefix_length
            );
    if(result != ERROR_SUCCESS) {
        return {.flags = IP_PARSE_FAILED};
    }

    if (addr_info.Format == NET_ADDRESS_IPV6) {
        /* The parsed IP is an IPv4 address */
        for(auto i = 0; i < 16; i++) {
            out.addr.ipv6.u.byte[i] = addr_info.Ipv6Address.sin6_addr.u.Byte[i];
        }
        out.flags = IP_FORMAT_IPV6;
        if(port) {
            out.flags |= IP_HAS_PORT;
            out.port = port;
        }
        if(prefix_length != 255) {
            out.flags |= IP_HAS_PREFIX_LENGTH;
            out.prefix_length = prefix_length;
        }
    } else if (addr_info.Format == NET_ADDRESS_IPV4) {
        /* The parsed IP is an IPv6 address */
        out.addr.ipv4 = addr_info.Ipv4Address.sin_addr.S_un.S_addr;
        out.flags = IP_FORMAT_IPV4;
        if(port) {
            out.flags |= IP_HAS_PORT;
            out.port = port;
        }
        if(prefix_length != 255) {
            out.flags |= IP_HAS_PREFIX_LENGTH;
            out.prefix_length = prefix_length;
        }
    } else {
        /* This route should not be possible, but I suppose we'll future proof IPv7? */
        return {.flags = IP_PARSE_FAILED};
    }
    return out;
}

const char* ip_test_vals[] = {
    /* IPv4 */
    "192.168.1.1",
    "10.0.0.1",
    "127.0.0.1",
    "0.0.0.0",
    "255.255.255.255",

    "192.168.1.1:80",
    "10.0.0.1:443",
    "127.0.0.1:1",
    "192.168.21.209:5201",
    "10.20.30.40:65535",

    "192.168.1.0/24",
    "10.0.0.0/8",
    "172.16.0.0/12",
    "192.168.21.209/32",
    "0.0.0.0/0",

    /* IPv6 */
    "::1",
    "::",
    "2001:db8::1",
    "fd7a:115c:a1e0::1",
    "fe80::1234:5678",
    "2001:db8:1234:5678:9abc:def0:1234:5678",

    "[::1]:80",
    "[2001:db8::1]:443",
    "[fd7a:115c:a1e0::1234]:502",
    "[fe80::1234:5678]:65535",
    "[2001:db8:1234:5678:9abc:def0:1234:5678]:1",

    "::/0",
    "::1/128",
    "2001:db8::/32",
    "fd7a:115c:a1e0::/48",
    "fe80::/10",
    "2001:db8:1234:5678::/64",
};

const char* ip_test_vals_fail[] = {
    /* IPv4 */
    "256.1.1.1",
    "192.168.1.256",
    "192.168.1",
    "192.168.1.1.1",
    "192..168.1.1",
    "192.168.-1.1",
    "abc.def.ghi.jkl",

    "192.168.1.1:",
    "192.168.1.1:-1",
    "192.168.1.1:65536",
    "192.168.1.1:abc",

    "192.168.1.0/",
    "192.168.1.0/-1",
    "192.168.1.0/33",
    "192.168.1.0/abc",

    "192.168.1.1:80/24",
    "192.168.1.1/24:80",

    /* IPv6 */
    ":",
    ":::",
    "2001:db8:::1",
    "2001:db8::gggg",
    "2001:db8::1::2",
    "2001:db8:1:2:3:4:5:6:7",

    "[2001:db8::1]",
    "[2001:db8::1]:",
    "[2001:db8::1]:abc",
    "[2001:db8::1]:65536",
    "[2001:db8::1:80",

    "2001:db8::/",
    "2001:db8::/129",
    "2001:db8::/-1",
    "2001:db8::/abc",

    "[2001:db8::1]:80/64",
    "[2001:db8::1/64]:80",
};

void test_ips() {
    for(auto i = 0; i < 32; i++) {
        auto out = parse_ip(ip_test_vals[i]);
        printf("Address Input: %s\n", ip_test_vals[i]);
        if(out.flags & IP_FORMAT_IPV4) {
            printf("Address: %d.%d.%d.%d\n",
                    out.addr.ipv4 & 0xff,
                    (out.addr.ipv4 & 0x0000ff00) >> 8,
                    (out.addr.ipv4 & 0x00ff0000) >> 16,
                    (out.addr.ipv4 & 0xff000000) >> 24);
            printf("Port: %d\n", out.port);
            printf("CIDR Prefix Length: %d\n", out.prefix_length);
            printf("Flags: IP_FORMAT_IPV4");
            if(out.flags & IP_HAS_PORT)
                printf(", IP_HAS_PORT");
            if(out.flags & IP_HAS_PREFIX_LENGTH)
                printf(", IP_HAS_PREFIX_LENGTH");
            printf("\n");
            printf("-------------\n");
        } else if (out.flags & IP_FORMAT_IPV6) {
            printf("Address: ");
            for(auto i = 0; i < 16; i++) {
                printf("%02x", out.addr.ipv6.u.byte[i]);
                if (i % 2 == 1)
                    printf(":");
            }
            printf("\n");
            printf("Port: %d\n", out.port);
            printf("CIDR Prefix Length: %d\n", out.prefix_length);
            printf("Flags: IP_FORMAT_IPV6");
            if(out.flags & IP_HAS_PORT)
                printf(", IP_HAS_PORT");
            if(out.flags & IP_HAS_PREFIX_LENGTH)
                printf(", IP_HAS_PREFIX_LENGTH");
            printf("\n");
            printf("-------------\n");
        } else if (out.flags & IP_PARSE_FAILED) {
            printf("Invalid IP Address\n");
            printf("-------------\n");
        }
    }

    for(auto i = 0; i < 34; i++) {
        auto out = parse_ip(ip_test_vals_fail[i]);
        printf("Address Input: %s\n", ip_test_vals_fail[i]);
        if(out.flags & IP_FORMAT_IPV4) {
            printf("Address: %d.%d.%d.%d\n",
                    out.addr.ipv4 & 0xff,
                    (out.addr.ipv4 & 0x0000ff00) >> 8,
                    (out.addr.ipv4 & 0x00ff0000) >> 16,
                    (out.addr.ipv4 & 0xff000000) >> 24);
            printf("Port: %d\n", out.port);
            printf("CIDR Prefix Length: %d\n", out.prefix_length);
            printf("Flags: IP_FORMAT_IPV4");
            if(out.flags & IP_HAS_PORT)
                printf(", IP_HAS_PORT");
            if(out.flags & IP_HAS_PREFIX_LENGTH)
                printf(", IP_HAS_PREFIX_LENGTH");
            printf("\n");
            printf("-------------\n");
        } else if (out.flags & IP_FORMAT_IPV6) {
            printf("Address: ");
            for(auto i = 0; i < 16; i++) {
                printf("%02x", out.addr.ipv6.u.byte[i]);
                if (i % 2 == 1)
                    printf(":");
            }
            printf("\n");
            printf("Port: %d\n", out.port);
            printf("CIDR Prefix Length: %d\n", out.prefix_length);
            printf("Flags: IP_FORMAT_IPV6");
            if(out.flags & IP_HAS_PORT)
                printf(", IP_HAS_PORT");
            if(out.flags & IP_HAS_PREFIX_LENGTH)
                printf(", IP_HAS_PREFIX_LENGTH");
            printf("\n");
            printf("-------------\n");
        } else if (out.flags & IP_PARSE_FAILED) {
            printf("Invalid IP Address\n");
            printf("-------------\n");
        }
    }
}

string ip_to_str(IP ip) {
    using std::to_string;
    using std::format;

    /* this is technically a really scuffed way of doing this...
     * just don't put in a bad input and we'll be alright */

    if (ip.flags & IP_FORMAT_IPV4) {
        string base = "",
               cidr = "",
               port = "";
        auto bytes = (u8*)&ip.addr.ipv4;

        if(ip.flags & IP_HAS_PREFIX_LENGTH)
            cidr = "/" + to_string(ip.prefix_length);

        if(ip.flags & IP_HAS_PORT)
            port = ":" + to_string(ip.port);

        base = to_string(bytes[0]) + "."
                + to_string(bytes[1]) + "."
                + to_string(bytes[2]) + "."
                + to_string(bytes[3]);

        return base + cidr + port;

    } else if (ip.flags & IP_FORMAT_IPV6) {
        string base = "",
               cidr = "",
               port = "";
        auto bytes = ip.addr.ipv6.u.byte;

        if(ip.flags & IP_HAS_PREFIX_LENGTH)
            cidr = "/" + to_string(ip.prefix_length);

        if(ip.flags & IP_HAS_PORT)
            port = ":" + to_string(ip.port);

        base = format("{:02x}", bytes[0])
                + format("{:02x}:", bytes[1])
                + format("{:02x}", bytes[2])
                + format("{:02x}:", bytes[3])
                + format("{:02x}", bytes[4])
                + format("{:02x}:", bytes[5])
                + format("{:02x}", bytes[6])
                + format("{:02x}:", bytes[7])
                + format("{:02x}", bytes[8])
                + format("{:02x}:", bytes[9])
                + format("{:02x}", bytes[10])
                + format("{:02x}:", bytes[11])
                + format("{:02x}", bytes[12])
                + format("{:02x}:", bytes[13])
                + format("{:02x}", bytes[14])
                + format("{:02x}", bytes[15]);

        if(ip.flags & IP_HAS_PORT)
            base = "[" + base + "]";

        return base + cidr + port;
    } else {
        /* TODO: care a little more here... */
        return "";
    }
}

EmbeddedResource load_resource(s32 resource_id) {
    HMODULE module = GetModuleHandle(0);
    if (!module)
        return {};
    HRSRC resource = FindResource(
            module,
            MAKEINTRESOURCE(resource_id),
            RT_RCDATA
            );
    if(!resource)
        return {};

    HGLOBAL loaded = LoadResource(module, resource);
    if(!loaded)
        return {};

    const DWORD size = SizeofResource(module, resource);
    const void* data = LockResource(loaded);
    if(!data || size == 0)
        return {};

    return {
        .data = data,
        .size = size
    };
}
