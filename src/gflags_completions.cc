// Copyright (c) 2008, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// ---

// Bash-style command line flag completion for C++ binaries
//
// This module implements bash-style completions.  It achieves this
// goal in the following broad chunks:
//
//  1) Take a to-be-completed word, and examine it for search hints
//  2) Identify all potentially matching flags
//     2a) If there are no matching flags, do nothing.
//     2b) If all matching flags share a common prefix longer than the
//         completion word, output just that matching prefix
//  3) Categorize those flags to produce a rough ordering of relevance.
//  4) Potentially trim the set of flags returned to a smaller number
//     that bash is happier with
//  5) Output the matching flags in groups ordered by relevance.
//     5a) Force bash to place most-relevent groups at the top of the list
//     5b) Trim most flag's descriptions to fit on a single terminal line

#include <cstdio>
#include <cstdlib>
#include <cstring>   // for strlen

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "gflags/gflags.h"
#include "gflags/gflags_completions.h"
#include "util.h"

using std::set;
using std::string;
using std::vector;


// 这是一种增强命令行用户体验的功能，允许用户输入部分命令行标志，然后程序会提供可能的补全选项
DEFINE_string(tab_completion_word, "",
              "If non-empty, HandleCommandLineCompletions() will hijack the "
              "process and attempt to do bash-style command line flag "
              "completion on this value.");
DEFINE_int32(tab_completion_columns, 80,
             "Number of columns to use in output for tab completion");


namespace GFLAGS_NAMESPACE {


namespace {
// Function prototypes and Type forward declarations.  Code may be
// more easily understood if it is roughly ordered according to
// control flow, rather than by C's "declare before use" ordering
struct CompletionOptions;
struct NotableFlags;

// The entry point if flag completion is to be used.
static void PrintFlagCompletionInfo(void);


// 1) Examine search word
static void CanonicalizeCursorWordAndSearchOptions(
    const string &cursor_word,
    string *canonical_search_token,
    CompletionOptions *options);

static bool RemoveTrailingChar(string *str, char c);


// 2) Find all matches
static void FindMatchingFlags(
    const vector<CommandLineFlagInfo> &all_flags,
    const CompletionOptions &options,
    const string &match_token,
    set<const CommandLineFlagInfo *> *all_matches,
    string *longest_common_prefix);

static bool DoesSingleFlagMatch(
    const CommandLineFlagInfo &flag,
    const CompletionOptions &options,
    const string &match_token);


// 3) Categorize matches
static void CategorizeAllMatchingFlags(
    const set<const CommandLineFlagInfo *> &all_matches,
    const string &search_token,
    const string &module,
    const string &package_dir,
    NotableFlags *notable_flags);

static void TryFindModuleAndPackageDir(
    const vector<CommandLineFlagInfo> &all_flags,
    string *module,
    string *package_dir);


// 4) Decide which flags to use
static void FinalizeCompletionOutput(
    const set<const CommandLineFlagInfo *> &matching_flags,
    CompletionOptions *options,
    NotableFlags *notable_flags,
    vector<string> *completions);

static void RetrieveUnusedFlags(
    const set<const CommandLineFlagInfo *> &matching_flags,
    const NotableFlags &notable_flags,
    set<const CommandLineFlagInfo *> *unused_flags);


// 5) Output matches
static void OutputSingleGroupWithLimit(
    const set<const CommandLineFlagInfo *> &group,
    const string &line_indentation,
    const string &header,
    const string &footer,
    bool long_output_format,
    int *remaining_line_limit,
    size_t *completion_elements_added,
    vector<string> *completions);

// (helpers for #5)
static string GetShortFlagLine(
    const string &line_indentation,
    const CommandLineFlagInfo &info);

static string GetLongFlagLine(
    const string &line_indentation,
    const CommandLineFlagInfo &info);


//
// Useful types

// Try to deduce the intentions behind this completion attempt.  Return the
// canonical search term in 'canonical_search_token'.  Binary search options
// are returned in the various booleans, which should all have intuitive
// semantics, possibly except:
//  - return_all_matching_flags: Generally, we'll trim the number of
//    returned candidates to some small number, showing those that are
//    most likely to be useful first.  If this is set, however, the user
//    really does want us to return every single flag as an option.
//  - force_no_update: Any time we output lines, all of which share a
//    common prefix, bash will 'helpfuly' not even bother to show the
//    output, instead changing the current word to be that common prefix.
//    If it's clear this shouldn't happen, we'll set this boolean

// force_no_update的解释：
// 你输入了 --he 并按下 Tab 键。
// 工具的自动补全功能发现了以下三个可能的选项：
// --help
// --hello
// --height
// 默认行为（force_no_update 为 false）
// 在默认情况下，bash 会自动将当前输入更新为这三个选项的公共前缀。
// 如果这三个选项的公共前缀是 --he，那么当前输入不会改变。
// 如果公共前缀是 --hel，那么当前输入会自动更新为 --hel。

// 设置 force_no_update 为 true
// 如果将 force_no_update 设置为 true，则即使这三个选项有一个公共前缀，
// 当前输入也不会自动更新。也就是说，输入仍然保持为 --he，而不会更新为 --hel。
struct CompletionOptions {
  bool flag_name_substring_search;
  bool flag_location_substring_search;
  bool flag_description_substring_search;
  bool return_all_matching_flags;
  bool force_no_update;
  CompletionOptions(): flag_name_substring_search(false),
                       flag_location_substring_search(false),
                       flag_description_substring_search(false),
                       return_all_matching_flags(false),
                       force_no_update(false) { }
};

// Notable flags are flags that are special or preferred for some
// reason.  For example, flags that are defined in the binary's module
// are expected to be much more relevant than flags defined in some
// other random location.  These sets are specified roughly in precedence
// order.  Once a flag is placed in one of these 'higher' sets, it won't
// be placed in any of the 'lower' sets.
struct NotableFlags {
  typedef set<const CommandLineFlagInfo *> FlagSet;
  FlagSet perfect_match_flag;
  FlagSet module_flags;       // Found in module file
  FlagSet package_flags;      // Found in same directory as module file
  FlagSet most_common_flags;  // One of the XXX most commonly supplied flags
  FlagSet subpackage_flags;   // Found in subdirectories of package
};


//
// Tab completion implementation - entry point
// 输出所有与搜索词匹配的flag
static void PrintFlagCompletionInfo(void) {
  string cursor_word = FLAGS_tab_completion_word;
  string canonical_token;
  CompletionOptions options = CompletionOptions();
  // 1、规范化搜索词，设置搜索选项，将搜索词存储在canonical_token中，搜索选项存储在options中
  CanonicalizeCursorWordAndSearchOptions(
      cursor_word,
      &canonical_token,
      &options);

  // 日志级别为1或更高时，输出一条包含canonical_token变量值的调试信息
  DVLOG(1) << "Identified canonical_token: '" << canonical_token << "'";

  vector<CommandLineFlagInfo> all_flags;
  set<const CommandLineFlagInfo *> matching_flags;
  GetAllFlags(&all_flags);
  DVLOG(2) << "Found " << all_flags.size() << " flags overall";

  string longest_common_prefix;
  // 2、找到所有与搜索词匹配的flag，并将其插入到matching_flags中，将这些flag的名字的最长公共前缀存储在longest_common_prefix中
  FindMatchingFlags(
      all_flags,
      options,
      canonical_token,
      &matching_flags,
      &longest_common_prefix);
  DVLOG(1) << "Identified " << matching_flags.size() << " matching flags";
  DVLOG(1) << "Identified " << longest_common_prefix
          << " as longest common prefix.";
  if (longest_common_prefix.size() > canonical_token.size()) {
    // There's actually a shared common prefix to all matching flags,
    // so may as well output that and quit quickly.
    DVLOG(1) << "The common prefix '" << longest_common_prefix
            << "' was longer than the token '" << canonical_token
            << "'.  Returning just this prefix for completion.";
    fprintf(stdout, "--%s", longest_common_prefix.c_str());
    return;
  }
  if (matching_flags.empty()) {
    VLOG(1) << "There were no matching flags, returning nothing.";
    return;
  }
  // module是整个二进制文件的路径，package_dir是module的父目录
  string module;
  string package_dir;
  // 3、找到所有flag中的模块文件和包目录，其中模块文件是filename，包目录是filename的父目录
  TryFindModuleAndPackageDir(all_flags, &module, &package_dir);
  DVLOG(1) << "Identified module: '" << module << "'";
  DVLOG(1) << "Identified package_dir: '" << package_dir << "'";

  NotableFlags notable_flags;
  // 4、将匹配的flag分为完全匹配、模块匹配、包匹配、常用匹配和子包匹配
  CategorizeAllMatchingFlags(
      matching_flags,
      canonical_token,
      module,
      package_dir,
      &notable_flags);
  DVLOG(2) << "Categorized matching flags:";
  DVLOG(2) << " perfect_match: " << notable_flags.perfect_match_flag.size();
  DVLOG(2) << " module: " << notable_flags.module_flags.size();
  DVLOG(2) << " package: " << notable_flags.package_flags.size();
  DVLOG(2) << " most common: " << notable_flags.most_common_flags.size();
  DVLOG(2) << " subpackage: " << notable_flags.subpackage_flags.size();

  vector<string> completions;
  // 5、将所有flag的信息输出到completions中，限制输出的行数不超过99行
  FinalizeCompletionOutput(
      matching_flags,
      &options,
      &notable_flags,
      &completions);

  if (options.force_no_update)
    completions.push_back("~");

  DVLOG(1) << "Finalized with " << completions.size()
          << " chosen completions";

  for (vector<string>::const_iterator it = completions.begin();
      it != completions.end();
      ++it) {
    DVLOG(9) << "  Completion entry: '" << *it << "'";
    fprintf(stdout, "%s\n", it->c_str());
  }
}


// 1) Examine search word (and helper method)
// 如果搜索词的后缀为?或者+，更新用于命令行自动补全的搜索选项
static void CanonicalizeCursorWordAndSearchOptions(
    const string &cursor_word,
    string *canonical_search_token,
    CompletionOptions *options) {
  *canonical_search_token = cursor_word;
  if (canonical_search_token->empty()) return;

  // Get rid of leading quotes and dashes in the search term
  if ((*canonical_search_token)[0] == '"')
    *canonical_search_token = canonical_search_token->substr(1);
  while ((*canonical_search_token)[0] == '-')
    *canonical_search_token = canonical_search_token->substr(1);

  options->flag_name_substring_search = false;
  options->flag_location_substring_search = false;
  options->flag_description_substring_search = false;
  options->return_all_matching_flags = false;
  options->force_no_update = false;

  // Look for all search options we can deduce now.  Do this by walking
  // backwards through the term, looking for up to three '?' and up to
  // one '+' as suffixed characters.  Consume them if found, and remove
  // them from the canonical search token.
  // ? 字符用于控制不同级别的子字符串搜索选项
  // + 字符用于控制是否返回所有匹配的标志
  int found_question_marks = 0;
  int found_plusses = 0;
  while (true) {
    if (found_question_marks < 3 &&
        RemoveTrailingChar(canonical_search_token, '?')) {
      ++found_question_marks;
      continue;
    }
    if (found_plusses < 1 &&
        RemoveTrailingChar(canonical_search_token, '+')) {
      ++found_plusses;
      continue;
    }
    break;
  }
  
  if (found_question_marks > 2) options->flag_description_substring_search = true;
  if (found_question_marks > 1) options->flag_location_substring_search = true;
  if (found_question_marks > 0) options->flag_name_substring_search = true;

  options->return_all_matching_flags = (found_plusses > 0);
}

// Returns true if a char was removed
static bool RemoveTrailingChar(string *str, char c) {
  if (str->empty()) return false;
  if ((*str)[str->size() - 1] == c) {
    *str = str->substr(0, str->size() - 1);
    return true;
  }
  return false;
}


// 2) Find all matches (and helper methods)
// 遍历所有flag，找到所有与搜索词匹配的flag，并将其地址插入到all_matches中
// 并且将这些flag的名字的最长公共前缀存储在longest_common_prefix中
static void FindMatchingFlags(
    const vector<CommandLineFlagInfo> &all_flags,
    const CompletionOptions &options,
    const string &match_token,
    set<const CommandLineFlagInfo *> *all_matches,
    string *longest_common_prefix) {
  all_matches->clear();
  bool first_match = true;
  for (vector<CommandLineFlagInfo>::const_iterator it = all_flags.begin();
      it != all_flags.end();
      ++it) {
    if (DoesSingleFlagMatch(*it, options, match_token)) {
      // 将匹配的flag的地址插入到all_matches中
      all_matches->insert(&*it);
      if (first_match) {
        first_match = false;
        // 将第一个匹配的flag的名字存储在longest_common_prefix中（初始化）
        *longest_common_prefix = it->name;
      } else {
        if (longest_common_prefix->empty() || it->name.empty()) {
          longest_common_prefix->clear();
          continue;
        }
        string::size_type pos = 0;
        // 寻找最长的公共前缀
        while (pos < longest_common_prefix->size() &&
            pos < it->name.size() &&
            (*longest_common_prefix)[pos] == it->name[pos])
          ++pos;
        // erase将删除pos位置之后的所有字符
        longest_common_prefix->erase(pos);
      }
    }
  }
}

// Given the set of all flags, the parsed match options, and the
// canonical search token, produce the set of all candidate matching
// flags for subsequent analysis or filtering.
// 分别检查flag的名字、位置和描述是否包含搜索词，如果包含则返回true
static bool DoesSingleFlagMatch(
    const CommandLineFlagInfo &flag,
    const CompletionOptions &options,
    const string &match_token) {
  // Is there a prefix match?
  string::size_type pos = flag.name.find(match_token);
  if (pos == 0) return true;

  // Is there a substring match if we want  it?
  if (options.flag_name_substring_search &&
      pos != string::npos)
    return true;

  // Is there a location match if we want it?
  if (options.flag_location_substring_search &&
      flag.filename.find(match_token) != string::npos)
    return true;

  // TODO(user): All searches should probably be case-insensitive
  // (especially this one...)
  if (options.flag_description_substring_search &&
      flag.description.find(match_token) != string::npos)
    return true;

  return false; 
}

// 3) Categorize matches (and helper method)

// Given a set of matching flags, categorize them by
// likely relevance to this specific binary
// 将匹配的flag分为完全匹配、模块匹配、包匹配、常用匹配和子包匹配
static void CategorizeAllMatchingFlags(
    const set<const CommandLineFlagInfo *> &all_matches,
    const string &search_token,
    const string &module,  // empty if we couldn't find any
    const string &package_dir,  // empty if we couldn't find any
    NotableFlags *notable_flags) {
  notable_flags->perfect_match_flag.clear();
  notable_flags->module_flags.clear();
  notable_flags->package_flags.clear();
  notable_flags->most_common_flags.clear();
  notable_flags->subpackage_flags.clear();

  for (set<const CommandLineFlagInfo *>::const_iterator it =
        all_matches.begin();
      it != all_matches.end();
      ++it) {
    DVLOG(2) << "Examining match '" << (*it)->name << "'";
    DVLOG(7) << "  filename: '" << (*it)->filename << "'";
    string::size_type pos = string::npos;
    if (!package_dir.empty())
      pos = (*it)->filename.find(package_dir);
    string::size_type slash = string::npos;
    if (pos != string::npos)  // candidate for package or subpackage match
      slash = (*it)->filename.find(
          PATH_SEPARATOR,
          pos + package_dir.size() + 1);

    // flag的名字与搜索词完全匹配
    if ((*it)->name == search_token) {
      // Exact match on some flag's name
      notable_flags->perfect_match_flag.insert(*it);
      DVLOG(3) << "Result: perfect match";
    // flag所在文件的文件名与模块文件名完全匹配
    } else if (!module.empty() && (*it)->filename == module) {
      // Exact match on module filename
      notable_flags->module_flags.insert(*it);
      DVLOG(3) << "Result: module match";
    // 标志所在的文件名包含给定的包目录，并且该文件名在包目录之后没有进一步的子目录（即没有更深层次的路径分隔符）
    } else if (!package_dir.empty() &&
        pos != string::npos && slash == string::npos) {
      // In the package, since there was no slash after the package portion
      notable_flags->package_flags.insert(*it);
      DVLOG(3) << "Result: package match";
    // 标志所在的文件名包含给定的包目录，并且该文件名在包目录之后有进一步的子目录（即有更深层次的路径分隔符）
    } else if (!package_dir.empty() &&
        pos != string::npos && slash != string::npos) {
      // In a subdirectory of the package
      notable_flags->subpackage_flags.insert(*it);
      DVLOG(3) << "Result: subpackage match";
    }

    DVLOG(3) << "Result: not special match";
  }
}

static void PushNameWithSuffix(vector<string>* suffixes, const char* suffix) {
  suffixes->push_back(
      StringPrintf("/%s%s", ProgramInvocationShortName(), suffix));
}

// 找到所有flag中的模块文件和包目录，其中模块文件是filename（路径），包目录是filename的父目录
static void TryFindModuleAndPackageDir(
    const vector<CommandLineFlagInfo> &all_flags,
    string *module,
    string *package_dir) {
  module->clear();
  package_dir->clear();

  vector<string> suffixes;
  // TODO(user): There's some inherant ambiguity here - multiple directories
  // could share the same trailing folder and file structure (and even worse,
  // same file names), causing us to be unsure as to which of the two is the
  // actual package for this binary.  In this case, we'll arbitrarily choose.
  PushNameWithSuffix(&suffixes, ".");
  PushNameWithSuffix(&suffixes, "-main.");
  PushNameWithSuffix(&suffixes, "_main.");
  // These four are new but probably merited?
  PushNameWithSuffix(&suffixes, "-test.");
  PushNameWithSuffix(&suffixes, "_test.");
  PushNameWithSuffix(&suffixes, "-unittest.");
  PushNameWithSuffix(&suffixes, "_unittest.");

  for (vector<CommandLineFlagInfo>::const_iterator it = all_flags.begin();
      it != all_flags.end();
      ++it) {
    for (vector<string>::const_iterator suffix = suffixes.begin();
        suffix != suffixes.end();
        ++suffix) {
      // TODO(user): Make sure the match is near the end of the string
      // 如果flag在这个二进制文件中定义，将其文件名存储在module中，将其父目录存储在package_dir中
      if (it->filename.find(*suffix) != string::npos) {
        *module = it->filename;
        string::size_type sep = it->filename.rfind(PATH_SEPARATOR);
        *package_dir = it->filename.substr(0, (sep == string::npos) ? 0 : sep);
        return;
      }
    }
  }
}

// Can't specialize template type on a locally defined type.  Silly C++...
struct DisplayInfoGroup {
  const char* header;
  const char* footer;
  set<const CommandLineFlagInfo *> *group;

  int SizeInLines() const {
    int size_in_lines = static_cast<int>(group->size()) + 1;
    if (strlen(header) > 0) {
      size_in_lines++;
    }
    if (strlen(footer) > 0) {
      size_in_lines++;
    }
    return size_in_lines;
  }
};

// 4) Finalize and trim output flag set
// 将所有flag的信息输出到completions中，限制输出的行数不超过99行
static void FinalizeCompletionOutput(
    const set<const CommandLineFlagInfo *> &matching_flags,
    CompletionOptions *options,
    NotableFlags *notable_flags,
    vector<string> *completions) {

  // We want to output lines in groups.  Each group needs to be indented
  // the same to keep its lines together.  Unless otherwise required,
  // only 99 lines should be output to prevent bash from harassing the
  // user.

  // First, figure out which output groups we'll actually use.  For each
  // nonempty group, there will be ~3 lines of header & footer, plus all
  // output lines themselves.
  // 确保输出的行数不超过99行
  int max_desired_lines =  // "999999 flags should be enough for anyone.  -dave"
    (options->return_all_matching_flags ? 999999 : 98);
  int lines_so_far = 0;

  vector<DisplayInfoGroup> output_groups;
  bool perfect_match_found = false;
  if (!notable_flags->perfect_match_flag.empty()) {
    perfect_match_found = true;
    DisplayInfoGroup group =
        { "",
          "==========",
          &notable_flags->perfect_match_flag };
    lines_so_far += group.SizeInLines();
    output_groups.push_back(group);
  }
  if (lines_so_far < max_desired_lines &&
      !notable_flags->module_flags.empty()) {
    DisplayInfoGroup group = {
        "-* Matching module flags *-",
        "===========================",
        &notable_flags->module_flags };
    lines_so_far += group.SizeInLines();
    output_groups.push_back(group);
  }
  if (lines_so_far < max_desired_lines &&
      !notable_flags->package_flags.empty()) {
    DisplayInfoGroup group = {
        "-* Matching package flags *-",
        "============================",
        &notable_flags->package_flags };
    lines_so_far += group.SizeInLines();
    output_groups.push_back(group);
  }
  if (lines_so_far < max_desired_lines &&
      !notable_flags->most_common_flags.empty()) {
    DisplayInfoGroup group = {
        "-* Commonly used flags *-",
        "=========================",
        &notable_flags->most_common_flags };
    lines_so_far += group.SizeInLines();
    output_groups.push_back(group);
  }
  if (lines_so_far < max_desired_lines &&
      !notable_flags->subpackage_flags.empty()) {
    DisplayInfoGroup group = {
        "-* Matching sub-package flags *-",
        "================================",
        &notable_flags->subpackage_flags };
    lines_so_far += group.SizeInLines();
    output_groups.push_back(group);
  }

  set<const CommandLineFlagInfo *> obscure_flags;  // flags not notable
  if (lines_so_far < max_desired_lines) {
    RetrieveUnusedFlags(matching_flags, *notable_flags, &obscure_flags);
    if (!obscure_flags.empty()) {
      DisplayInfoGroup group = {
          "-* Other flags *-",
          "",
          &obscure_flags };
      lines_so_far += group.SizeInLines();
      output_groups.push_back(group);
    }
  }

  // Second, go through each of the chosen output groups and output
  // as many of those flags as we can, while remaining below our limit
  int remaining_lines = max_desired_lines;
  size_t completions_output = 0;
  int indent = static_cast<int>(output_groups.size()) - 1;
  for (vector<DisplayInfoGroup>::const_iterator it =
        output_groups.begin();
      it != output_groups.end();
      ++it, --indent) {
    OutputSingleGroupWithLimit(
        *it->group,  // group
        string(indent, ' '),  // line indentation
        string(it->header),  // header
        string(it->footer),  // footer
        perfect_match_found,  // long format
        &remaining_lines,  // line limit - reduces this by number printed
        &completions_output,  // completions (not lines) added
        completions);  // produced completions
    perfect_match_found = false;
  }

  if (completions_output != matching_flags.size()) {
    options->force_no_update = false;
    completions->push_back("~ (Remaining flags hidden) ~");
  } else {
    options->force_no_update = true;
  }
}

// 将未使用的flag（即不属于任何特殊类别的flag）插入到unused_flags中
static void RetrieveUnusedFlags(
    const set<const CommandLineFlagInfo *> &matching_flags,
    const NotableFlags &notable_flags,
    set<const CommandLineFlagInfo *> *unused_flags) {
  // Remove from 'matching_flags' set all members of the sets of
  // flags we've already printed (specifically, those in notable_flags)
  for (set<const CommandLineFlagInfo *>::const_iterator it =
        matching_flags.begin();
      it != matching_flags.end();
      ++it) {
    if (notable_flags.perfect_match_flag.count(*it) ||
        notable_flags.module_flags.count(*it) ||
        notable_flags.package_flags.count(*it) ||
        notable_flags.most_common_flags.count(*it) ||
        notable_flags.subpackage_flags.count(*it))
      continue;
    unused_flags->insert(*it);
  }
}

// 5) Output matches (and helper methods)

// 在completion中输出一个flag组，包括header、footer和group中的所有flag，限制行数不超过remaining_line_limit
static void OutputSingleGroupWithLimit(
    const set<const CommandLineFlagInfo *> &group,
    const string &line_indentation,
    const string &header,
    const string &footer,
    bool long_output_format,
    int *remaining_line_limit,
    size_t *completion_elements_output,
    vector<string> *completions) {
  if (group.empty()) return;
  if (!header.empty()) {
    if (*remaining_line_limit < 2) return;
    *remaining_line_limit -= 2;
    completions->push_back(line_indentation + header);
    completions->push_back(line_indentation + string(header.size(), '-'));
  }
  for (set<const CommandLineFlagInfo *>::const_iterator it = group.begin();
      it != group.end() && *remaining_line_limit > 0;
      ++it) {
    --*remaining_line_limit;
    ++*completion_elements_output;
    completions->push_back(
        (long_output_format
          ? GetLongFlagLine(line_indentation, **it)
          : GetShortFlagLine(line_indentation, **it)));
  }
  if (!footer.empty()) {
    if (*remaining_line_limit < 1) return;
    --*remaining_line_limit;
    completions->push_back(line_indentation + footer);
  }
}

// 返回flag的简短信息，包括flag的名字、默认值和描述，其中描述信息可能会被截断
static string GetShortFlagLine(
    const string &line_indentation,
    const CommandLineFlagInfo &info) {
  string prefix;
  bool is_string = (info.type == "string");
  SStringPrintf(&prefix, "%s--%s [%s%s%s] ",
                line_indentation.c_str(),
                info.name.c_str(),
                (is_string ? "'" : ""),
                info.default_value.c_str(),
                (is_string ? "'" : ""));
  int remainder =
      FLAGS_tab_completion_columns - static_cast<int>(prefix.size());
  string suffix;
  if (remainder > 0)
  // 如果flag的描述信息长度大于剩余的列数，将其截断
    suffix =
        (static_cast<int>(info.description.size()) > remainder ?
         (info.description.substr(0, remainder - 3) + "...").c_str() :
         info.description.c_str());
  return prefix + suffix;
}

// 返回flag具体的描述信息，包括flag的名字、描述、类型、默认值、当前值与所定义的文件名
static string GetLongFlagLine(
    const string &line_indentation,
    const CommandLineFlagInfo &info) {

  string output = DescribeOneFlag(info);

  // Replace '-' with '--', and remove trailing newline before appending
  // the module definition location.
  string old_flagname = "-" + info.name;
  output.replace(
      output.find(old_flagname),
      old_flagname.size(),
      "-" + old_flagname);
  // Stick a newline and indentation in front of the type and default
  // portions of DescribeOneFlag()s description
  static const char kNewlineWithIndent[] = "\n    ";
  output.replace(output.find(" type:"), 1, string(kNewlineWithIndent));
  output.replace(output.find(" default:"), 1, string(kNewlineWithIndent));
  output = StringPrintf("%s Details for '--%s':\n"
                        "%s    defined: %s",
                        line_indentation.c_str(),
                        info.name.c_str(),
                        output.c_str(),
                        info.filename.c_str());

  // Eliminate any doubled newlines that crept in.  Specifically, if
  // DescribeOneFlag() decided to break the line just before "type"
  // or "default", we don't want to introduce an extra blank line
  static const string line_of_spaces(FLAGS_tab_completion_columns, ' ');
  static const char kDoubledNewlines[] = "\n     \n";
  for (string::size_type newlines = output.find(kDoubledNewlines);
      newlines != string::npos;
      newlines = output.find(kDoubledNewlines))
    // Replace each 'doubled newline' with a single newline
    output.replace(newlines, sizeof(kDoubledNewlines) - 1, string("\n"));

  for (string::size_type newline = output.find('\n');
      newline != string::npos;
      newline = output.find('\n')) {
    // 得到换行符在当前行中的列位置
    int newline_pos = static_cast<int>(newline) % FLAGS_tab_completion_columns;
    int missing_spaces = FLAGS_tab_completion_columns - newline_pos;
    // 将换行符替换为空格，实现换行
    output.replace(newline, 1, line_of_spaces, 1, missing_spaces);
  }
  return output;
}
}  // anonymous

// 这个文件中只有这一个函数是提供给外部调用的，因为它不在匿名命名空间中
void HandleCommandLineCompletions(void) {
  if (FLAGS_tab_completion_word.empty()) return;
  PrintFlagCompletionInfo();
  gflags_exitfunc(0);
}


} // namespace GFLAGS_NAMESPACE
