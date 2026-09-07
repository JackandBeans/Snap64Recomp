#ifndef __RECOMP_FILES_H__
#define __RECOMP_FILES_H__

#include <filesystem>
#include <fstream>
#include <span>

namespace recomp {
    // Opens filepath, or its backup when filepath cannot be opened. A filepath that exists but is
    // incomplete opens fine and is returned as-is; fixed-size images should use read_file_with_backup.
    std::ifstream open_input_file_with_backup(const std::filesystem::path& filepath, std::ios_base::openmode mode = std::ios_base::in);
    std::ifstream open_input_backup_file(const std::filesystem::path& filepath, std::ios_base::openmode mode = std::ios_base::in);
    // Fills contents from filepath when it is exactly contents.size() bytes and reads in full, otherwise
    // from its backup under the same test, logging which file was used and why when it is not filepath.
    // Returns false when neither is usable; contents is then unspecified. used_backup, when given, says
    // whether the backup had to stand in, which tells the caller that what sits at filepath is not a
    // usable image and must not be rotated into the backup by the next write.
    bool read_file_with_backup(const std::filesystem::path& filepath, std::span<char> contents, bool* used_backup = nullptr);
    // Opens filepath's temporary file for writing. Close the stream, then call
    // finalize_output_file_with_backup to publish it.
    std::ofstream open_output_file_with_backup(const std::filesystem::path& filepath, std::ios_base::openmode mode = std::ios_base::out);
    // Publishes the temporary file: filepath is renamed to its backup and the temporary file is renamed
    // to filepath, each atomically. The temporary file is not forced to disk first.
    bool finalize_output_file_with_backup(const std::filesystem::path& filepath);
    // Writes contents to filepath's temporary file, forces it to disk, then publishes it. A crash at any
    // point leaves filepath and its backup each either absent or a whole copy of some contents that was
    // passed in; neither ever holds a half-written file.
    // rotate_backup says whether what is currently at filepath may become the new backup. Pass false when
    // the caller does not know that file to be usable -- it was never read, or it was read and rejected --
    // so that a bad copy cannot push the last good one out of the backup. The temporary file then replaces
    // filepath by a single rename and the backup is left exactly as it is.
    bool write_file_with_backup(const std::filesystem::path& filepath, std::span<const char> contents, bool rotate_backup = true);
};

#endif
