/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Files, directories, KeyValues trees and the streaming KeyValues reader
 * (ISource2ModFiles, ISource2ModKeyValues).
 *
 * Project: Source2Mod
 */
#pragma once

#include <source2mod/ISource2ModFiles.h>

#include <memory>
#include <string>
#include <vector>

namespace files
{
    /// One key of a tree: a section (children) or a value.
    class KeyValues final : public ISource2ModKeyValues
    {
    public:
        const char* GetName() override { return m_name.c_str(); }
        void SetName(const char* name) override { m_name = name ? name : ""; }

        ISource2ModKeyValues* FindKey(const char* key, bool create = false) override;
        ISource2ModKeyValues* GetFirstSubKey() override;
        ISource2ModKeyValues* GetNextKey() override;
        ISource2ModKeyValues* GetFirstTrueSubKey() override;
        ISource2ModKeyValues* GetNextTrueSubKey() override;
        ISource2ModKeyValues* GetFirstValue() override;
        ISource2ModKeyValues* GetNextValue() override;

        ESource2ModKvType GetDataType(const char* key = nullptr) override;
        const char* GetString(const char* key = nullptr, const char* def = "") override;
        int GetInt(const char* key = nullptr, int def = 0) override;
        float GetFloat(const char* key = nullptr, float def = 0.0f) override;
        uint64_t GetUint64(const char* key = nullptr, uint64_t def = 0) override;
        uint32_t GetColor(const char* key = nullptr, uint32_t def = 0) override;

        void SetString(const char* key, const char* value) override;
        void SetInt(const char* key, int value) override;
        void SetFloat(const char* key, float value) override;
        void SetUint64(const char* key, uint64_t value) override;
        void SetColor(const char* key, uint32_t rgba) override;

        bool RemoveKey(const char* key) override;
        void CopySubkeys(ISource2ModKeyValues* from) override;
        void Merge(ISource2ModKeyValues* from) override;
        ISource2ModKeyValues* Clone() override;

        bool LoadFromFile(const char* path, bool escapes = false) override;
        bool LoadFromString(const char* text, bool escapes = false) override;
        bool SaveToFile(const char* path, bool escapes = false) override;
        size_t SaveToString(char* buffer, size_t maxlen, bool escapes = false) override;

        void Release() override;

        // ---- core side ----

        /// A key directly under this one (no paths); nullptr when missing.
        KeyValues* Child(const std::string& name) const;
        KeyValues* AddChild(const std::string& name);
        bool IsSection() const { return m_type == SOURCE2MOD_KV_NONE; }
        KeyValues* Parent() const { return m_parent; }
        const std::vector<std::unique_ptr<KeyValues>>& Children() const { return m_children; }
        /// Removes a direct child; false when it is not one.
        bool RemoveChild(KeyValues* child);
        /// The whole tree under this key as text.
        std::string ToText(bool escapes) const;
        void CopyFrom(const KeyValues& other);

    private:
        KeyValues* Next(bool sections, bool values) const;
        KeyValues* First(bool sections, bool values) const;
        void SetValue(ESource2ModKvType type);
        void Write(std::string& out, int depth, bool escapes) const;

        std::string m_name;
        ESource2ModKvType m_type = SOURCE2MOD_KV_NONE;
        std::string m_string;
        int64_t m_int = 0;          // Int, Uint64, Color
        float m_float = 0.0f;
        mutable std::string m_text; // GetString's buffer for a non-string value
        std::vector<std::unique_ptr<KeyValues>> m_children;
        KeyValues* m_parent = nullptr;
    };

    class FilesManager final : public ISource2ModFiles
    {
    public:
        std::string ResolvePath(const char* path) override;
        bool FileExists(const char* path) override;
        bool DirExists(const char* path) override;
        int64_t FileSize(const char* path) override;
        int64_t FileTime(const char* path) override;
        bool ReadFile(const char* path, std::string& contents) override;
        bool WriteFile(const char* path, const void* data, size_t size, bool append = false) override;
        bool DeleteFile(const char* path) override;
        bool RenameFile(const char* from, const char* to) override;
        bool CreateDirectory(const char* path) override;
        bool RemoveDirectory(const char* path) override;
        bool ListDirectory(const char* path, Source2ModDirCallback callback) override;
        ISource2ModKeyValues* CreateKeyValues(const char* name) override;
        ESource2ModKvError ParseKeyValues(const char* text, ISource2ModKvListener* listener,
                                          int* line = nullptr, int* col = nullptr, bool escapes = true) override;
        ESource2ModKvError ParseKeyValuesFile(const char* path, ISource2ModKvListener* listener,
                                              int* line = nullptr, int* col = nullptr, bool escapes = true) override;
        const char* KvErrorString(ESource2ModKvError error) override;
    };

    extern FilesManager filesManager;
}
