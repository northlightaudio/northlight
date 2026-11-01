#include "engine/rt/RtKit.h"

#include "engine/rt/RealtimeGuard.h"

#if defined(__linux__)
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#endif

namespace northlight::rt::rtkit
{

#if defined(__linux__)

namespace
{
// The parts of libdbus's C API used here, declared as dbus-1 declares them. The two
// structs are allocated by the caller; these are at least as large as libdbus's.
struct DBusError
{
    const char* name;
    const char* message;
    unsigned int flags;
    void* padding[4];
};
struct DBusMessageIter
{
    alignas(void*) unsigned char storage[128];
};
struct DBusConnection;
struct DBusMessage;
using dbus_bool_t = std::uint32_t;

constexpr int systemBus = 1; // DBUS_BUS_SYSTEM
constexpr int typeInvalid = 0, typeInt32 = 'i', typeUint32 = 'u', typeInt64 = 'x', typeUint64 = 't',
              typeString = 's', typeVariant = 'v';

constexpr const char* service = "org.freedesktop.RealtimeKit1";
constexpr const char* objectPath = "/org/freedesktop/RealtimeKit1";
constexpr int timeoutMilliseconds = 1000;

struct DBus
{
    void (*errorInit)(DBusError*) = nullptr;
    void (*errorFree)(DBusError*) = nullptr;
    DBusConnection* (*busGetPrivate)(int, DBusError*) = nullptr;
    void (*setExitOnDisconnect)(DBusConnection*, dbus_bool_t) = nullptr;
    void (*close)(DBusConnection*) = nullptr;
    void (*connectionUnref)(DBusConnection*) = nullptr;
    DBusMessage* (*newMethodCall)(const char*, const char*, const char*, const char*) = nullptr;
    dbus_bool_t (*appendArgs)(DBusMessage*, int, ...) = nullptr;
    DBusMessage* (*sendWithReplyAndBlock)(DBusConnection*, DBusMessage*, int, DBusError*) = nullptr;
    void (*messageUnref)(DBusMessage*) = nullptr;
    dbus_bool_t (*iterInit)(DBusMessage*, DBusMessageIter*) = nullptr;
    int (*iterArgType)(DBusMessageIter*) = nullptr;
    void (*iterRecurse)(DBusMessageIter*, DBusMessageIter*) = nullptr;
    void (*iterGetBasic)(DBusMessageIter*, void*) = nullptr;
    bool loaded = false;
};

DBus loadDBus() noexcept
{
    DBus api;
    // Stays loaded for the life of the process.
    void* library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr)
        return api;
    bool ok = true;
    const auto load = [&](auto& function, const char* name)
    {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
        ok = ok && function != nullptr;
    };
    load(api.errorInit, "dbus_error_init");
    load(api.errorFree, "dbus_error_free");
    load(api.busGetPrivate, "dbus_bus_get_private");
    load(api.setExitOnDisconnect, "dbus_connection_set_exit_on_disconnect");
    load(api.close, "dbus_connection_close");
    load(api.connectionUnref, "dbus_connection_unref");
    load(api.newMethodCall, "dbus_message_new_method_call");
    load(api.appendArgs, "dbus_message_append_args");
    load(api.sendWithReplyAndBlock, "dbus_connection_send_with_reply_and_block");
    load(api.messageUnref, "dbus_message_unref");
    load(api.iterInit, "dbus_message_iter_init");
    load(api.iterArgType, "dbus_message_iter_get_arg_type");
    load(api.iterRecurse, "dbus_message_iter_recurse");
    load(api.iterGetBasic, "dbus_message_iter_get_basic");
    api.loaded = ok;
    return api;
}

const DBus& dbus() noexcept
{
    static const DBus api = loadDBus();
    return api;
}

// A private connection to the system bus for one request, closed when done.
class Connection
{
public:
    Connection() noexcept
    {
        if (!api_.loaded)
            return;
        DBusError error{};
        api_.errorInit(&error);
        connection_ = api_.busGetPrivate(systemBus, &error);
        api_.errorFree(&error);
        if (connection_ != nullptr)
            api_.setExitOnDisconnect(connection_, 0); // a lost bus must not end the app
    }
    ~Connection()
    {
        if (connection_ != nullptr)
        {
            api_.close(connection_);
            api_.connectionUnref(connection_);
        }
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    explicit operator bool() const noexcept { return connection_ != nullptr; }

    // Sends `message` (and releases it); the reply, which the caller releases, or
    // nullptr on an error.
    DBusMessage* call(DBusMessage* message) noexcept
    {
        DBusError error{};
        api_.errorInit(&error);
        auto* reply = api_.sendWithReplyAndBlock(connection_, message, timeoutMilliseconds, &error);
        api_.errorFree(&error);
        api_.messageUnref(message);
        return reply;
    }

private:
    const DBus& api_ = dbus();
    DBusConnection* connection_ = nullptr;
};

// One of rtkit's integer properties.
std::optional<std::int64_t> property(Connection& connection, const char* name) noexcept
{
    const auto& api = dbus();
    auto* message = api.newMethodCall(service, objectPath, "org.freedesktop.DBus.Properties", "Get");
    if (message == nullptr)
        return std::nullopt;
    const char* interface = service;
    if (api.appendArgs(message, typeString, &interface, typeString, &name, typeInvalid) == 0)
    {
        api.messageUnref(message);
        return std::nullopt;
    }
    auto* reply = connection.call(message);
    if (reply == nullptr)
        return std::nullopt;
    std::optional<std::int64_t> value;
    DBusMessageIter outer{}, inner{};
    if (api.iterInit(reply, &outer) != 0 && api.iterArgType(&outer) == typeVariant)
    {
        api.iterRecurse(&outer, &inner);
        switch (api.iterArgType(&inner))
        {
            case typeInt32:
            {
                std::int32_t v = 0;
                api.iterGetBasic(&inner, &v);
                value = v;
                break;
            }
            case typeUint32:
            {
                std::uint32_t v = 0;
                api.iterGetBasic(&inner, &v);
                value = v;
                break;
            }
            case typeInt64:
            {
                std::int64_t v = 0;
                api.iterGetBasic(&inner, &v);
                value = v;
                break;
            }
            case typeUint64:
            {
                std::uint64_t v = 0;
                api.iterGetBasic(&inner, &v);
                value = static_cast<std::int64_t>(std::min<std::uint64_t>(v, INT64_MAX));
                break;
            }
            default:
                break;
        }
    }
    api.messageUnref(reply);
    return value;
}

std::uint64_t currentThreadId() noexcept
{
    return static_cast<std::uint64_t>(syscall(SYS_gettid));
}
} // namespace

int makeCurrentThreadRealtime(int priority) noexcept
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("rtkit::makeCurrentThreadRealtime");
    Connection connection;
    if (!connection)
        return 0;
    const auto maxPriority = property(connection, "MaxRealtimePriority");
    const auto maxTime = property(connection, "RTTimeUSecMax");
    if (!maxPriority || !maxTime || *maxPriority < 1 || *maxTime < 1)
        return 0;
    const auto granted = static_cast<std::uint32_t>(std::clamp<std::int64_t>(priority, 1, *maxPriority));

    // rtkit grants real-time only to a process whose RLIMIT_RTTIME is at most its
    // maximum, so a runaway real-time thread cannot hold a core for ever.
    rlimit limit{};
    if (getrlimit(RLIMIT_RTTIME, &limit) != 0)
        return 0;
    const auto most = static_cast<rlim_t>(*maxTime);
    if (limit.rlim_max == RLIM_INFINITY || limit.rlim_max > most)
        limit.rlim_max = most;
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur > limit.rlim_max)
        limit.rlim_cur = limit.rlim_max;
    if (setrlimit(RLIMIT_RTTIME, &limit) != 0)
        return 0;

    const auto& api = dbus();
    auto* message = api.newMethodCall(service, objectPath, service, "MakeThreadRealtime");
    if (message == nullptr)
        return 0;
    const std::uint64_t thread = currentThreadId();
    if (api.appendArgs(message, typeUint64, &thread, typeUint32, &granted, typeInvalid) == 0)
    {
        api.messageUnref(message);
        return 0;
    }
    auto* reply = connection.call(message);
    if (reply == nullptr)
        return 0;
    api.messageUnref(reply);
    return static_cast<int>(granted);
}

bool makeCurrentThreadHighPriority(int nice) noexcept
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("rtkit::makeCurrentThreadHighPriority");
    Connection connection;
    if (!connection)
        return false;
    const auto minNice = property(connection, "MinNiceLevel");
    if (!minNice)
        return false;
    const auto level = static_cast<std::int32_t>(std::max<std::int64_t>(nice, *minNice));

    const auto& api = dbus();
    auto* message = api.newMethodCall(service, objectPath, service, "MakeThreadHighPriority");
    if (message == nullptr)
        return false;
    const std::uint64_t thread = currentThreadId();
    if (api.appendArgs(message, typeUint64, &thread, typeInt32, &level, typeInvalid) == 0)
    {
        api.messageUnref(message);
        return false;
    }
    auto* reply = connection.call(message);
    if (reply == nullptr)
        return false;
    api.messageUnref(reply);
    return true;
}

#else

int makeCurrentThreadRealtime(int) noexcept
{
    return 0;
}

bool makeCurrentThreadHighPriority(int) noexcept
{
    return false;
}

#endif

} // namespace northlight::rt::rtkit
