/*******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.    *
 ******************************************************************************/

#include "common/CredentialFile.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <stdexcept>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using Entries = std::vector<std::pair<std::string, std::string>>;

class CredentialFileTester : public ::testing::Test {
protected:
  void SetUp() override {
    dir = std::filesystem::temp_directory_path() /
          ("cudaq_credfile_" + std::to_string(::getpid()) + "_" +
           ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::create_directories(dir);
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::permissions(dir / "cfg", std::filesystem::perms::owner_all,
                                 ec);
    std::filesystem::remove_all(dir, ec);
  }

  std::string write(const std::string &contents) {
    auto path = dir / "cfg";
    std::ofstream out(path, std::ios::binary);
    out << contents;
    return path.string();
  }

  static cudaq::CredentialFileFormat simpleFormat() {
    cudaq::CredentialFileFormat f;
    f.allowedKeys = {"key", "refresh", "time"};
    f.formatHint = "`<key> : <value>` format. (One per line)";
    return f;
  }

  static Entries parse(const std::string &path,
                       const cudaq::CredentialFileFormat &f) {
    Entries entries;
    cudaq::parseCredentialFile(path, f,
                               [&](const std::string &k, const std::string &v) {
                                 entries.emplace_back(k, v);
                               });
    return entries;
  }

  std::filesystem::path dir;
};

} // namespace

TEST_F(CredentialFileTester, parsesNormalFile) {
  auto entries =
      parse(write("key:abc\nrefresh:def\ntime:123\n"), simpleFormat());
  EXPECT_EQ(entries, (Entries{{"key", "abc"}, {"refresh", "def"},
                              {"time", "123"}}));
}

TEST_F(CredentialFileTester, lastLineNeedsNoNewline) {
  auto entries = parse(write("key:abc\nrefresh:def"), simpleFormat());
  EXPECT_EQ(entries, (Entries{{"key", "abc"}, {"refresh", "def"}}));
}

TEST_F(CredentialFileTester, trimsWhitespaceAroundKeyAndValue) {
  auto entries =
      parse(write("  key  :  abc  \n\trefresh :\tdef\t\r\n time : 99 \n"),
            simpleFormat());
  EXPECT_EQ(entries, (Entries{{"key", "abc"}, {"refresh", "def"},
                              {"time", "99"}}));
}

TEST_F(CredentialFileTester, emptyFileHasNoEntries) {
  EXPECT_TRUE(parse(write(""), simpleFormat()).empty());
}

TEST_F(CredentialFileTester, fileWithOnlyANewlineIsIllFormed) {
  // The lone newline is a blank line, which is not a key-value pair.
  EXPECT_THROW(parse(write("\n"), simpleFormat()), std::runtime_error);
}

TEST_F(CredentialFileTester, missingKeyIsNotAnError) {
  // Requiring particular keys is up to the caller.
  auto entries = parse(write("key:abc\n"), simpleFormat());
  EXPECT_EQ(entries, (Entries{{"key", "abc"}}));
}

TEST_F(CredentialFileTester, unknownKeyThrows) {
  try {
    parse(write("key:abc\nbogus:1\n"), simpleFormat());
    FAIL() << "expected an exception";
  } catch (const std::runtime_error &e) {
    EXPECT_STREQ(e.what(), "Unknown key in configuration file: bogus.");
  }
}

TEST_F(CredentialFileTester, keysAreCaseSensitive) {
  EXPECT_THROW(parse(write("KEY:abc\n"), simpleFormat()), std::runtime_error);
}

TEST_F(CredentialFileTester, valueContainingColonIsIllFormed) {
  auto path = write("key:a:b\n");
  try {
    parse(path, simpleFormat());
    FAIL() << "expected an exception";
  } catch (const std::runtime_error &e) {
    EXPECT_EQ(std::string(e.what()),
              "Ill-formed configuration file (" + path +
                  "). Key-value pairs must be in `<key> : <value>` format. "
                  "(One per line)");
  }
}

TEST_F(CredentialFileTester, lineWithoutColonOrValueIsIllFormed) {
  EXPECT_THROW(parse(write("key abc\n"), simpleFormat()), std::runtime_error);
  EXPECT_THROW(parse(write("key:\n"), simpleFormat()), std::runtime_error);
}

TEST_F(CredentialFileTester, blankAndCommentLinesAreIllFormed) {
  EXPECT_THROW(parse(write("key:abc\n\nrefresh:def\n"), simpleFormat()),
               std::runtime_error);
  EXPECT_THROW(parse(write("   \nkey:abc\n"), simpleFormat()),
               std::runtime_error);
  EXPECT_THROW(parse(write("# comment\nkey:abc\n"), simpleFormat()),
               std::runtime_error);
  // A comment containing a colon splits into a (bogus) key and a value.
  EXPECT_THROW(parse(write("# note: hi\nkey:abc\n"), simpleFormat()),
               std::runtime_error);
}

TEST_F(CredentialFileTester, duplicateKeysAreReportedInFileOrder) {
  auto entries = parse(write("key:one\nrefresh:r\nkey:two\n"), simpleFormat());
  EXPECT_EQ(entries, (Entries{{"key", "one"}, {"refresh", "r"},
                              {"key", "two"}}));
  std::map<std::string, std::string> last;
  for (auto &[k, v] : entries)
    last[k] = v;
  EXPECT_EQ(last["key"], "two");
}

TEST_F(CredentialFileTester, errorsAreRaisedInFileOrder) {
  // The callback has already seen the first line when the second one fails.
  Entries seen;
  EXPECT_THROW(cudaq::parseCredentialFile(
                   write("key:abc\nbogus:1\n"), simpleFormat(),
                   [&](const std::string &k, const std::string &v) {
                     seen.emplace_back(k, v);
                   }),
               std::runtime_error);
  EXPECT_EQ(seen, (Entries{{"key", "abc"}}));
}

TEST_F(CredentialFileTester, callbackExceptionsPropagate) {
  EXPECT_THROW(cudaq::parseCredentialFile(
                   write("key:abc\n"), simpleFormat(),
                   [](const std::string &, const std::string &) {
                     throw std::logic_error("from callback");
                   }),
               std::logic_error);
}

TEST_F(CredentialFileTester, missingFileBehavesLikeEmptyFile) {
  EXPECT_TRUE(
      parse((dir / "does-not-exist").string(), simpleFormat()).empty());
}

TEST_F(CredentialFileTester, unreadableFileBehavesLikeEmptyFile) {
  if (::geteuid() == 0)
    GTEST_SKIP() << "permissions are not enforced for root";
  auto path = write("key:abc\n");
  std::filesystem::permissions(path, std::filesystem::perms::none);
  EXPECT_TRUE(parse(path, simpleFormat()).empty());
}

TEST_F(CredentialFileTester, multiColonKeyKeepsItsColons) {
  auto f = simpleFormat();
  f.allowedKeys.push_back("credentials");
  f.multiColonKey = "credentials";
  auto entries = parse(write("key:abc\ncredentials: {username:u, password:p}\n"
                             "refresh:r\n"),
                       f);
  EXPECT_EQ(entries, (Entries{{"key", "abc"},
                              {"credentials", "{username:u, password:p}"},
                              {"refresh", "r"}}));
}

TEST_F(CredentialFileTester, multiColonKeyMustBeWrittenWithoutSpaceBeforeColon) {
  auto f = simpleFormat();
  f.allowedKeys.push_back("credentials");
  f.multiColonKey = "credentials";
  EXPECT_THROW(parse(write("credentials : {username:u, password:p}\n"), f),
               std::runtime_error);
}

TEST_F(CredentialFileTester, multiColonKeyNeedsItsFourPieces) {
  auto f = simpleFormat();
  f.allowedKeys.push_back("credentials");
  f.multiColonKey = "credentials";
  EXPECT_THROW(parse(write("credentials:x\n"), f), std::runtime_error);
  EXPECT_THROW(parse(write("credentials:a:b\n"), f), std::runtime_error);
  EXPECT_THROW(parse(write("credentials:a:b:c:d\n"), f), std::runtime_error);
}

TEST_F(CredentialFileTester, otherKeysCannotUseMultiColonForm) {
  auto f = simpleFormat();
  f.allowedKeys.push_back("credentials");
  f.multiColonKey = "credentials";
  EXPECT_THROW(parse(write("key:a:b:c\n"), f), std::runtime_error);
}

TEST_F(CredentialFileTester, withoutMultiColonKeyCredentialsIsUnknown) {
  try {
    parse(write("credentials:x\n"), simpleFormat());
    FAIL() << "expected an exception";
  } catch (const std::runtime_error &e) {
    EXPECT_STREQ(e.what(), "Unknown key in configuration file: credentials.");
  }
}
