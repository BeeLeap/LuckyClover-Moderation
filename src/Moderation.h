#pragma once

#include <utility>

#include <ll/api/event/Listener.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerChatEvent.h>
#include <ll/api/event/world/ServerLevelTickEvent.h>
#include <ll/api/mod/NativeMod.h>
#include <ila/event/world/actor/player/PlayerEditSignEvent.h>

class NetworkIdentifier;
class BlockActorDataPacket;
class ServerPlayer;
class BookEditPacket;
class ItemStackRequestPacket;

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace lucky_clover::moderation {

using SignEditEvent = ila::mc::world::actor::player::PlayerEditSignBeforeEvent;

class Moderation final {
    struct TrieNode {
        std::unordered_map<unsigned char, std::unique_ptr<TrieNode>> children;
        bool terminal = false;
    };

public:
    static Moderation& getInstance();

    bool load();
    bool enable();
    bool disable();

private:
    Moderation();

    struct Config {
        bool enabled = true;
        std::string replacement = "***";
        std::string wordListPath = "data/banned-words";
        std::vector<std::string> customWords;
        std::vector<std::string> whitelist;
        bool logMatches = true;
        bool migrated = false;
        std::unordered_map<std::string, bool> categories;
    };

    void loadConfig();
    void saveConfig();
    bool loadWords();
    void reloadIfRequested();
    void writeStatus();
    void insertWord(TrieNode& root, std::string const& word);
    void onChat(ll::event::PlayerChatEvent& event);
    void onEditSign(SignEditEvent& event);
    bool onHangingSignPacket(ServerPlayer& player, std::shared_ptr<BlockActorDataPacket> const& packet);
    bool moderateText(std::string& text);
    std::string normalize(std::string const& text) const;
    std::string filter(std::string const& text, bool& matched, size_t& matchCount) const;
    std::string resolvePath(std::string const& path) const;
    bool tryMigrateLegacyConfig();

    ll::mod::NativeMod& mSelf;
    Config mConfig;
    std::unordered_set<std::string> mWords;
    std::unordered_set<std::string> mWhitelist;
    TrieNode mTrie;
    TrieNode mWhitelistTrie;
    std::shared_ptr<ll::event::Listener<ll::event::PlayerChatEvent>> mChatListener;
    std::shared_ptr<ll::event::Listener<ll::event::ServerLevelTickEvent>> mTickListener;
    std::shared_ptr<ll::event::Listener<SignEditEvent>> mSignListener;
    std::filesystem::file_time_type mReloadSignal = std::filesystem::file_time_type::min();

    friend struct HangingSignEditPacketHook;
    friend struct BookEditPacketHook;
    friend struct ItemStackRequestPacketHook;
    friend struct TextPacketHook;
};

} // namespace lucky_clover::moderation

