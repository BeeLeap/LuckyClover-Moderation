#include "Moderation.h"

#include <ll/api/event/EventBus.h>
#include <ll/api/memory/Hook.h>
#include <ll/api/mod/RegisterHelper.h>
#include <ll/api/io/Logger.h>
#include <ila/base/Gloabl.h>
#include <mc/network/Packet/BlockActorDataPacket.h>
#include <mc/network/packet/BookEditPacket.h>
#include <mc/network/packet/ItemStackRequestPacket.h>
#include <mc/network/packet/TextPacket.h>
#include <mc/network/packet/item_stack_request_packet_data/RequestData.h>
#include <mc/network/packet/book_edit_action/AddPage.h>
#include <mc/network/packet/book_edit_action/Finalize.h>
#include <mc/network/packet/book_edit_action/ReplacePage.h>
#include <mc/network/ServerNetworkHandler.h>
#include <mc/world/level/BlockSource.h>
#include <mc/world/level/block/actor/SignBlockActor.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <variant>

namespace lucky_clover::moderation {

using json = nlohmann::json;
const std::unordered_map<std::string, std::filesystem::path> CategoryFiles = {
    {"adult", L"色情类.txt"},
    {"political", L"政治类.txt"},
    {"advertising", L"广告.txt"},
    {"weapons", L"涉枪涉爆违法信息关键词.txt"},
    {"urls", L"网址.txt"},
};

LL_TYPE_INSTANCE_HOOK(
    HangingSignEditPacketHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&              source,
    std::shared_ptr<BlockActorDataPacket> packet
) {
    if (!packet) {
        origin(source, packet);
        return;
    }
    auto* player = thisFor<NetEventCallback>()->_getServerPlayer(source, packet->mSenderSubId);
    if (player && Moderation::getInstance().onHangingSignPacket(*player, packet)) return;
    origin(source, packet);
}

LL_TYPE_INSTANCE_HOOK(
    BookEditPacketHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const& source,
    BookEditPacket const& packet
) {
    auto mutablePacket = packet;
    std::visit(
        [](auto& action) {
            using Action = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<Action, BookEditAction::AddPage> ||
                          std::is_same_v<Action, BookEditAction::ReplacePage>) {
                Moderation::getInstance().moderateText(action.mPageText);
            } else if constexpr (std::is_same_v<Action, BookEditAction::Finalize>) {
                Moderation::getInstance().moderateText(action.mTitle);
                Moderation::getInstance().moderateText(action.mAuthor);
            }
        },
        mutablePacket.mOperation.get()
    );
    origin(source, mutablePacket);
}

LL_TYPE_INSTANCE_HOOK(
    ItemStackRequestPacketHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&       source,
    ItemStackRequestPacket const& packet
) {
    auto mutablePacket = packet;
    for (auto& request : mutablePacket.mRequests.get()) {
        for (auto& text : request.mStringsToFilter.get()) {
            Moderation::getInstance().moderateText(text);
        }
    }
    origin(source, mutablePacket);
}

LL_TYPE_INSTANCE_HOOK(
    TextPacketHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const& source,
    TextPacket const& packet
) {
    auto mutablePacket = packet;
    std::visit(
        [](auto& body) {
            using Body = std::decay_t<decltype(body)>;
            if constexpr (std::is_same_v<Body, TextPacketPayload::AuthorAndMessage> ||
                          std::is_same_v<Body, TextPacketPayload::MessageAndParams> ||
                          std::is_same_v<Body, TextPacketPayload::MessageOnly>) {
                if (body.mType == TextPacketType::Chat || body.mType == TextPacketType::Whisper ||
                    body.mType == TextPacketType::Raw) {
                    Moderation::getInstance().moderateText(body.mMessage);
                }
            }
        },
        mutablePacket.mBody.get()
    );
    origin(source, mutablePacket);
}

Moderation& Moderation::getInstance() {
    static Moderation instance;
    return instance;
}

Moderation::Moderation() : mSelf(*ll::mod::NativeMod::current()) {}

bool Moderation::load() {
    try {
        loadConfig();
        if (!loadWords()) return false;
        return true;
    } catch (std::exception const& error) {
        mSelf.getLogger().error("Moderation load failed: {}", error.what());
        return false;
    } catch (...) {
        mSelf.getLogger().error("Moderation load failed with an unknown exception");
        return false;
    }
}

bool Moderation::enable() {
    mChatListener = ll::event::Listener<ll::event::PlayerChatEvent>::create(
        [this](ll::event::PlayerChatEvent& event) { onChat(event); },
        ll::event::EventPriority::Highest
    );
    if (!ll::event::EventBus::getInstance().addListener(mChatListener)) {
        mChatListener.reset();
        mSelf.getLogger().error("Failed to subscribe to PlayerChatEvent");
        return false;
    }

    mTickListener = ll::event::Listener<ll::event::ServerLevelTickEvent>::create(
        [this](ll::event::ServerLevelTickEvent&) { reloadIfRequested(); },
        ll::event::EventPriority::Lowest
    );
    if (!ll::event::EventBus::getInstance().addListener(mTickListener)) {
        mTickListener.reset();
        mSelf.getLogger().warn("Failed to subscribe to ServerLevelTickEvent; reload will occur on the next text event");
    }

    mSignListener = ll::event::Listener<SignEditEvent>::create(
        [this](SignEditEvent& event) { onEditSign(event); },
        ll::event::EventPriority::Highest
    );
    if (!ll::event::EventBus::getInstance().addListener(mSignListener)) {
        mSignListener.reset();
        mSelf.getLogger().error("Failed to subscribe to PlayerEditSignBeforeEvent");
        if (mTickListener) {
            ll::event::EventBus::getInstance().removeListener(mTickListener);
            mTickListener.reset();
        }
        if (mChatListener) {
            ll::event::EventBus::getInstance().removeListener(mChatListener);
            mChatListener.reset();
        }
        return false;
    }

    if (HangingSignEditPacketHook::hook() != 0 || BookEditPacketHook::hook() != 0 ||
        ItemStackRequestPacketHook::hook() != 0 || TextPacketHook::hook() != 0) {
        mSelf.getLogger().error("Failed to install one or more custom-text hooks");
        TextPacketHook::unhook();
        ItemStackRequestPacketHook::unhook();
        HangingSignEditPacketHook::unhook();
        BookEditPacketHook::unhook();
        ll::event::EventBus::getInstance().removeListener(mSignListener);
        mSignListener.reset();
        return false;
    }

    mSelf.getLogger().info("Moderation enabled; loaded {} words", mWords.size());
    return true;
}

bool Moderation::disable() {
    TextPacketHook::unhook();
    ItemStackRequestPacketHook::unhook();
    BookEditPacketHook::unhook();
    HangingSignEditPacketHook::unhook();
    if (mChatListener) {
        ll::event::EventBus::getInstance().removeListener(mChatListener);
        mChatListener.reset();
    }
    if (mTickListener) {
        ll::event::EventBus::getInstance().removeListener(mTickListener);
        mTickListener.reset();
    }
    if (mSignListener) {
        ll::event::EventBus::getInstance().removeListener(mSignListener);
        mSignListener.reset();
    }
    return true;
}

void Moderation::loadConfig() {
    auto path = std::filesystem::path(mSelf.getModDir()) / "config.json";
    std::ifstream input(path);
    if (!input) {
        if (tryMigrateLegacyConfig()) return;
        mSelf.getLogger().warn("Config not found at {}; using defaults", path.string());
        saveConfig();
        return;
    }

    try {
        json value;
        input >> value;
        if (value.contains("enabled")) mConfig.enabled = value.at("enabled").get<bool>();
        if (value.contains("replacement")) mConfig.replacement = value.at("replacement").get<std::string>();
        if (value.contains("wordListPath")) mConfig.wordListPath = value.at("wordListPath").get<std::string>();
        if (value.contains("customWords")) mConfig.customWords = value.at("customWords").get<std::vector<std::string>>();
        if (value.contains("whitelist")) mConfig.whitelist = value.at("whitelist").get<std::vector<std::string>>();
        if (value.contains("logMatches")) mConfig.logMatches = value.at("logMatches").get<bool>();
        if (value.contains("migrated")) mConfig.migrated = value.at("migrated").get<bool>();
        if (value.contains("categories")) mConfig.categories = value.at("categories").get<std::unordered_map<std::string, bool>>();
    } catch (std::exception const& error) {
        mSelf.getLogger().error("Invalid config.json: {}; using defaults", error.what());
    }
}

void Moderation::saveConfig() {
    try {
        auto path = std::filesystem::path(mSelf.getModDir()) / "config.json";
        json value = {
            {"enabled", mConfig.enabled},
            {"replacement", mConfig.replacement},
            {"wordListPath", mConfig.wordListPath},
            {"customWords", mConfig.customWords},
            {"whitelist", mConfig.whitelist},
            {"logMatches", mConfig.logMatches},
            {"migrated", mConfig.migrated},
            {"categories", mConfig.categories},
        };
        std::ofstream output(path);
        output << value.dump(4) << '\n';
    } catch (std::exception const& error) {
        mSelf.getLogger().warn("Failed to save moderation config: {}", error.what());
    }
}

bool Moderation::tryMigrateLegacyConfig() {
    auto legacyPath = std::filesystem::path("plugins/LuckyClover-Plugin/config.json");
    std::ifstream input(legacyPath);
    if (!input) return false;

    try {
        json root;
        input >> root;
        auto const& banned = root.at("bannedWords");
        if (banned.contains("enabled")) mConfig.enabled = banned.at("enabled").get<bool>();
        if (banned.contains("replacement")) mConfig.replacement = banned.at("replacement").get<std::string>();
        if (banned.contains("customWords")) mConfig.customWords = banned.at("customWords").get<std::vector<std::string>>();
        if (banned.contains("whitelist")) mConfig.whitelist = banned.at("whitelist").get<std::vector<std::string>>();
        if (banned.contains("logMatches")) mConfig.logMatches = banned.at("logMatches").get<bool>();
        if (banned.contains("categories")) mConfig.categories = banned.at("categories").get<std::unordered_map<std::string, bool>>();
        mConfig.migrated = true;
        saveConfig();
        mSelf.getLogger().info("Migrated bannedWords configuration from LuckyClover-Plugin");
        return true;
    } catch (std::exception const& error) {
        mSelf.getLogger().warn("Legacy bannedWords migration skipped: {}", error.what());
        return false;
    }
}

bool Moderation::loadWords() {
    std::string stage = "initializing word lists";
    auto previousConfig = mConfig;
    auto previousWords = mWords;
    auto previousWhitelist = mWhitelist;
    auto previousTrie = std::move(mTrie);
    auto previousWhitelistTrie = std::move(mWhitelistTrie);
    try {
        mSelf.getLogger().info("Loading moderation word lists");
        mWords.clear();
        mWhitelist.clear();
        mTrie.children.clear();
        mTrie.terminal = false;
        mWhitelistTrie.children.clear();
        mWhitelistTrie.terminal = false;

        stage = "loading whitelist";
        for (auto const& word : mConfig.whitelist) {
            auto normalized = normalize(word);
            if (!normalized.empty()) {
                mWhitelist.insert(normalized);
                insertWord(mWhitelistTrie, normalized);
            }
        }

        stage = "locating word list directory";
        auto directory = std::filesystem::path(resolvePath(mConfig.wordListPath));
        if (!std::filesystem::exists(directory)) {
            auto legacy = std::filesystem::path("plugins/LuckyClover-Plugin/data/banned-words");
            if (std::filesystem::exists(legacy)) directory = legacy;
        }
        if (std::filesystem::exists(directory)) {
            stage = "reading built-in word lists";
            for (auto const& [category, fileName] : CategoryFiles) {
                if (mConfig.categories.contains(category) && !mConfig.categories.at(category)) continue;
                auto entry = directory / fileName;
                if (!std::filesystem::is_regular_file(entry)) continue;
                std::ifstream input(entry);
                std::string line;
                while (std::getline(input, line)) {
                    auto normalized = normalize(line);
                    if (!normalized.empty()) mWords.insert(std::move(normalized));
                }
            }
        } else {
            mSelf.getLogger().warn("Word list directory not found: {}", directory.string());
        }

        stage = "loading custom words";
        for (auto const& word : mConfig.customWords) {
            auto normalized = normalize(word);
            if (!normalized.empty()) mWords.insert(std::move(normalized));
        }
        for (auto const& word : mWhitelist) mWords.erase(word);

        mSelf.getLogger().info("Loaded {} moderation words; building trie", mWords.size());
        stage = "building moderation trie";
        for (auto const& word : mWords) insertWord(mTrie, word);

        stage = "writing moderation status";
        writeStatus();
        mSelf.getLogger().info("Moderation word lists ready; loaded {} words", mWords.size());
        return true;
    } catch (std::exception const& error) {
        mConfig = std::move(previousConfig);
        mWords = std::move(previousWords);
        mWhitelist = std::move(previousWhitelist);
        mTrie = std::move(previousTrie);
        mWhitelistTrie = std::move(previousWhitelistTrie);
        mSelf.getLogger().error("Moderation word-list load failed during {}: {}", stage, error.what());
        return false;
    } catch (...) {
        mConfig = std::move(previousConfig);
        mWords = std::move(previousWords);
        mWhitelist = std::move(previousWhitelist);
        mTrie = std::move(previousTrie);
        mWhitelistTrie = std::move(previousWhitelistTrie);
        mSelf.getLogger().error("Moderation word-list load failed during {} with an unknown exception", stage);
        return false;
    }
}

void Moderation::insertWord(TrieNode& root, std::string const& word) {
    auto* node = &root;
    for (unsigned char byte : word) {
        auto& child = node->children[byte];
        if (!child) child = std::make_unique<TrieNode>();
        node = child.get();
    }
    node->terminal = true;
}

void Moderation::reloadIfRequested() {
    auto signalPath = std::filesystem::path(mSelf.getModDir()) / "reload.request";
    std::error_code error;
    auto signal = std::filesystem::last_write_time(signalPath, error);
    if (error || signal <= mReloadSignal) return;
    mReloadSignal = signal;
    loadConfig();
    if (!loadWords()) {
        mSelf.getLogger().warn("Moderation reload failed after rebuilding word-list state");
        return;
    }
    mSelf.getLogger().info("Moderation configuration reloaded");
}

void Moderation::writeStatus() {
    try {
        auto path = std::filesystem::path(mSelf.getModDir()) / "status.json";
        json value = {
            {"enabled", mConfig.enabled},
            {"loaded", true},
            {"count", mWords.size()},
        };
        std::ofstream output(path);
        output << value.dump(4) << '\n';
    } catch (std::exception const& error) {
        mSelf.getLogger().warn("Failed to save moderation status: {}", error.what());
    }
}

std::string Moderation::resolvePath(std::string const& path) const {
    std::filesystem::path value(path);
    if (value.is_absolute()) return value.string();
    return (std::filesystem::path(mSelf.getModDir()) / value).lexically_normal().string();
}

std::string Moderation::normalize(std::string const& text) const {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (i + 2 < text.size() && c == 0xEF && static_cast<unsigned char>(text[i + 1]) == 0xBC) {
            unsigned char tail = static_cast<unsigned char>(text[i + 2]);
            if (tail >= 0x81 && tail <= 0xBE) {
                result.push_back(static_cast<char>(tail - 0x60));
                i += 3;
                continue;
            }
        }
        if (c < 0x80) result.push_back(static_cast<char>(std::tolower(c)));
        else result.append(text, i, 1);
        ++i;
    }
    return result;
}

std::string Moderation::filter(std::string const& text, bool& matched, size_t& matchCount) const {
    matched = false;
    matchCount = 0;
    std::string normalized;
    std::vector<size_t> mapStart;
    std::vector<size_t> mapEnd;
    for (size_t index = 0; index < text.size();) {
        size_t width = 1;
        unsigned char first = static_cast<unsigned char>(text[index]);
        if ((first & 0x80) == 0) width = 1;
        else if ((first & 0xE0) == 0xC0) width = 2;
        else if ((first & 0xF0) == 0xE0) width = 3;
        else if ((first & 0xF8) == 0xF0) width = 4;
        if (index + width > text.size()) width = 1;
        auto unit = text.substr(index, width);
        auto normalizedUnit = normalize(unit);
        normalized += normalizedUnit;
        for (size_t n = 0; n < normalizedUnit.size(); ++n) {
            mapStart.push_back(index);
            mapEnd.push_back(index + width);
        }
        index += width;
    }
    std::vector<std::pair<size_t, size_t>> ranges;
    auto findRanges = [&](TrieNode const& root) {
        std::vector<std::pair<size_t, size_t>> result;
        for (size_t start = 0; start < normalized.size(); ++start) {
            auto const* node = &root;
            for (size_t cursor = start; cursor < normalized.size(); ++cursor) {
                auto child = node->children.find(static_cast<unsigned char>(normalized[cursor]));
                if (child == node->children.end()) break;
                node = child->second.get();
                if (node->terminal && cursor < mapEnd.size()) {
                    result.emplace_back(mapStart[start], mapEnd[cursor]);
                }
            }
        }
        return result;
    };
    auto allowedRanges = findRanges(mWhitelistTrie);
    auto bannedRanges = findRanges(mTrie);
    for (auto const& [start, end] : bannedRanges) {
        bool allowed = false;
        for (auto const& [allowedStart, allowedEnd] : allowedRanges) {
            if (start < allowedEnd && allowedStart < end) {
                allowed = true;
                break;
            }
        }
        if (!allowed) ranges.emplace_back(start, end);
    }
    /*
     * Ranges are mapped back to original UTF-8 byte offsets. Overlapping
     * matches are collapsed below so a single replacement is emitted.
     */
    if (ranges.empty()) return text;
    matched = true;
    std::sort(ranges.begin(), ranges.end());
    std::string output;
    size_t cursor = 0;
    for (auto const& [start, end] : ranges) {
        if (start < cursor) continue;
        output.append(text, cursor, start - cursor);
        output += mConfig.replacement.empty() ? "***" : mConfig.replacement;
        cursor = end;
        ++matchCount;
    }
    output.append(text, cursor, std::string::npos);
    return output;
}

void Moderation::onChat(ll::event::PlayerChatEvent& event) {
    reloadIfRequested();
    if (!mConfig.enabled || mWords.empty()) return;
    try {
        bool matched = false;
        size_t count = 0;
        auto filtered = filter(event.message(), matched, count);
        if (!matched) return;
        event.message() = filtered;
        if (mConfig.logMatches) mSelf.getLogger().info("Filtered {} banned chat match(es)", count);
    } catch (std::exception const& error) {
        mSelf.getLogger().warn("Chat moderation failed: {}", error.what());
    }
}

bool Moderation::moderateText(std::string& text) {
    try {
        reloadIfRequested();
        if (!mConfig.enabled || mWords.empty()) return false;
        bool matched = false;
        size_t count = 0;
        auto filtered = filter(text, matched, count);
        if (!matched) return false;
        text = std::move(filtered);
        if (mConfig.logMatches) mSelf.getLogger().info("Filtered {} banned custom-text match(es)", count);
        return true;
    } catch (std::exception const& error) {
        mSelf.getLogger().warn("Custom text moderation failed: {}", error.what());
        return false;
    } catch (...) {
        mSelf.getLogger().warn("Custom text moderation failed with an unknown exception");
        return false;
    }
}

void Moderation::onEditSign(SignEditEvent& event) {
    reloadIfRequested();
    if (!mConfig.enabled || mWords.empty()) return;
    try {
        bool matched = false;
        size_t count = 0;
        auto filtered = filter(event.text(), matched, count);
        if (!matched) return;
        event.text() = filtered;
        if (mConfig.logMatches) mSelf.getLogger().info("Filtered {} banned sign match(es)", count);
    } catch (std::exception const& error) {
        mSelf.getLogger().warn("Sign moderation failed: {}", error.what());
    }
}

bool Moderation::onHangingSignPacket(ServerPlayer& player, std::shared_ptr<BlockActorDataPacket> const& packet) {
    if (!packet || !packet->mData->contains("id", Tag::Type::String) ||
        (*packet->mData)["id"] != "HangingSign") {
        return false;
    }
    if (!packet->mData->contains("FrontText", Tag::Type::Compound) ||
        !packet->mData->contains("BackText", Tag::Type::Compound) ||
        !packet->mData.get()["FrontText"].contains("Text", Tag::Type::String) ||
        !packet->mData.get()["BackText"].contains("Text", Tag::Type::String)) {
        return false;
    }

    auto* blockActor = static_cast<SignBlockActor*>(
        player.getDimensionBlockSource().getBlockEntity(packet->mPos)
    );
    if (!blockActor) return false;

    bool frontEdit = blockActor->mTextFront->getMessage() != packet->mData.get()["FrontText"]["Text"];
    bool backEdit = blockActor->mTextBack->getMessage() != packet->mData.get()["BackText"]["Text"];

    if (frontEdit) {
        auto beforeEvent = SignEditEvent(
            player,
            packet->mPos,
            (*packet->mData)["FrontText"]["Text"].get<StringTag>(),
            SignTextSide::Front
        );
        LLEventBus.publish(beforeEvent);
        if (beforeEvent.isCancelled()) return true;
        (*packet->mData)["FrontText"]["Text"].get<StringTag>() = beforeEvent.text();
    }
    if (backEdit) {
        auto beforeEvent = SignEditEvent(
            player,
            packet->mPos,
            (*packet->mData)["BackText"]["Text"].get<StringTag>(),
            SignTextSide::Back
        );
        LLEventBus.publish(beforeEvent);
        if (beforeEvent.isCancelled()) return true;
        (*packet->mData)["BackText"]["Text"].get<StringTag>() = beforeEvent.text();
    }
    return false;
}

LL_REGISTER_MOD(Moderation, Moderation::getInstance());

} // namespace lucky_clover::moderation
