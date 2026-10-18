"use strict";

const PLUGIN_NAME = "LuckyClover-Moderation-API";
const PLUGIN_VERSION = [0, 1, 0];
const NAMESPACE = "LuckyCloverModeration";
const MODERATION_CONFIG = "plugins/LuckyClover-Moderation/config.json";
const WORD_LIST_DIR = "plugins/LuckyClover-Moderation/data/banned-words/";
const CATEGORY_FILES = {
    adult: "色情类.txt",
    political: "政治类.txt",
    advertising: "广告.txt",
    weapons: "涉枪涉爆违法信息关键词.txt",
    urls: "网址.txt",
};

ll.registerPlugin(PLUGIN_NAME, "LuckyClover moderation API bridge", PLUGIN_VERSION, { Author: "Mell" });
logger.setTitle(PLUGIN_NAME);

let state = null;

function readText(path) {
    try {
        return typeof File.readFrom === "function" ? String(File.readFrom(path) || "") : "";
    } catch (error) {
        return "";
    }
}

function parseJson(path, fallback) {
    try {
        const value = JSON.parse(readText(path));
        return value && typeof value === "object" ? value : fallback;
    } catch (error) {
        return fallback;
    }
}

function normalize(value) {
    return String(value || "")
        .normalize("NFKC")
        .toLowerCase()
        .replace(/[\u0000-\u001f]/g, "");
}

function loadState() {
    const config = Object.assign({
        enabled: true,
        replacement: "***",
        customWords: [],
        whitelist: [],
        categories: {},
    }, parseJson(MODERATION_CONFIG, {}));
    const words = [];
    Object.keys(CATEGORY_FILES).forEach((category) => {
        if (config.categories && config.categories[category] === false) return;
        readText(WORD_LIST_DIR + CATEGORY_FILES[category]).split(/\r?\n/).forEach((line) => {
            const word = normalize(line.trim());
            if (word) words.push(word);
        });
    });
    (Array.isArray(config.customWords) ? config.customWords : []).forEach((word) => {
        const normalized = normalize(word).trim();
        if (normalized) words.push(normalized);
    });
    const whitelist = new Set((Array.isArray(config.whitelist) ? config.whitelist : [])
        .map((word) => normalize(word).trim()).filter(Boolean));
    state = {
        enabled: config.enabled !== false,
        replacement: String(config.replacement || "***"),
        words: Array.from(new Set(words)).filter((word) => !whitelist.has(word)).sort((a, b) => b.length - a.length),
        whitelist,
    };
}

function filterText(value) {
    const original = String(value === null || value === undefined ? "" : value);
    if (!state) loadState();
    if (!state.enabled || !state.words.length || !original) return original;
    let output = original;
    state.words.forEach((word) => {
        if (!word || state.whitelist.has(word)) return;
        const normalized = normalize(output);
        if (normalized.indexOf(word) < 0) return;
        const escaped = word.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
        try { output = output.replace(new RegExp(escaped, "giu"), state.replacement); } catch (error) { /* keep original */ }
    });
    return output;
}

function reload() {
    loadState();
    return { ok: true, words: state.words.length };
}

ll.export(filterText, NAMESPACE, "filterText");
ll.export((value) => Boolean(filterText(value) !== String(value === null || value === undefined ? "" : value)), NAMESPACE, "containsBannedWord");
ll.export(reload, NAMESPACE, "reload");
loadState();
if (typeof mc.setInterval === "function") mc.setInterval(loadState, 5000);

