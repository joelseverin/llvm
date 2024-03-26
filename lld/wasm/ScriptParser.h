//===- ScriptParser.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_SCRIPT_PARSER_H
#define LLD_WASM_SCRIPT_PARSER_H

#include "ScriptParser.h"
#include "OutputSections.h"
#include "ScriptLexer.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <limits>
#include <vector>

namespace lld::wasm {

// This enum is used to implement linker script SECTIONS command.
// https://sourceware.org/binutils/docs/ld/SECTIONS.html#SECTIONS
enum SectionsCommandKind {
  AssignmentKind, // . = expr or <sym> = expr
  OutputSectionKind,
  InputSectionKind,
  ByteKind    // BYTE(expr), SHORT(expr), LONG(expr) or QUAD(expr)
};

struct SectionCommand {
  SectionCommand(int k) : kind(k) {}
  int kind;
};

class SectionBase {
public:
/*
  enum Kind { Regular, Synthetic, EHFrame, Merge, Output };

  Kind kind() const { return (Kind)sectionKind; }

  uint8_t sectionKind : 3;

  // The next two bit fields are only used by InputSectionBase, but we
  // put them here so the struct packs better.

  uint8_t bss : 1;

  // Set for sections that should not be folded by ICF.
  uint8_t keepUnique : 1;

  uint8_t partition = 1;
*/
//  uint32_t type;
  //union {
    OutputSection *outputSection;
    //InputChunk *inputChunk;
  //};

  StringRef name;

  uint64_t address;
  //uint32_t addralign;
  bool live;

  SmallVector<SectionCommand*, 0> commands;
  std::string location;

/*
  // The 1-indexed partition that this section is assigned to by the garbage
  // collector, or 0 if this section is dead. Normally there is only one
  // partition, so this will either be 0 or 1.
  elf::Partition &getPartition() const;

  // These corresponds to the fields in Elf_Shdr.
  uint64_t flags;
  uint32_t addralign;
  uint32_t entsize;
  uint32_t link;
  uint32_t info;
*/

  OutputSection *getOutputSection() { return outputSection; }
  const OutputSection *getOutputSection() const {
    return const_cast<SectionBase *>(this)->getOutputSection();
  }

  // Translate an offset in the input section to an offset in the output
  // section.
  uint64_t getOffset(uint64_t offset) const { return offset; }

  uint64_t getVA(uint64_t offset = 0) const { return offset; };

  bool isLive() const { return live; } //return partition != 0; }
  void markLive() { live = 1; }
  void markDead() { live = 0; }

  SectionBase(OutputSection *osec) : outputSection(osec), name(osec->name) {}

/*
protected:
  constexpr SectionBase(/*Kind sectionKind,/ StringRef name, uint64_t flags,
                        uint32_t entsize, uint32_t addralign, uint32_t type,
                        uint32_t info, uint32_t link)
      : name (name) {}
//      : sectionKind(sectionKind), bss(false), keepUnique(false), type(type),
//        name(name), flags(flags), addralign(addralign), entsize(entsize),
//        link(link), info(info) {}
*/
};

// This represents an r-value in the linker script.
struct ExprValue {
  ExprValue(SectionBase *sec, bool forceAbsolute, uint64_t val,
            const Twine &loc)
      : sec(sec), val(val), forceAbsolute(forceAbsolute), loc(loc.str()) {}

  ExprValue(uint64_t val) : ExprValue(nullptr, false, val, "") {}

  bool isAbsolute() const { return forceAbsolute || sec == nullptr; }
  uint64_t getValue() const;
  uint64_t getSecAddr() const;
  uint64_t getSectionOffset() const;

  // If a value is relative to a section, it has a non-null Sec.
  SectionBase *sec;

  uint64_t val;
  uint64_t alignment = 1;

  // True if this expression is enclosed in ABSOLUTE().
  // This flag affects the return value of getValue().
  bool forceAbsolute;

  // Original source location. Used for error messages.
  std::string loc;
};

// This represents an expression in the linker script.
// ScriptParser::readExpr reads an expression and returns an Expr.
// Later, we evaluate the expression by calling the function.
using Expr = std::function<ExprValue()>;

// This represents ". = <expr>" or "<symbol> = <expr>".
struct SymbolAssignment : SectionCommand {
  SymbolAssignment(StringRef name, Expr e, std::string loc)
      : SectionCommand(AssignmentKind), name(name), expression(e),
        location(loc) {}

  static bool classof(const SectionCommand *c) {
    return c->kind == AssignmentKind;
  }

  // The LHS of an expression. Name is either a symbol name or ".".
  StringRef name;
  DefinedData *sym = nullptr;

  // The RHS of an expression.
  Expr expression;

  // Command attributes for PROVIDE, HIDDEN and PROVIDE_HIDDEN.
  bool provide = false;
  bool hidden = false;

  // Holds file name and line number for error reporting.
  std::string location;

  // A string representation of this command. We use this for -Map.
  std::string commandString;

  // Address of this assignment command.
  uint64_t addr;

  // Size of this assignment command. This is usually 0, but if
  // you move '.' this may be greater than 0.
  uint64_t size;
};

struct OutputDesc final : SectionCommand {
  SectionBase osec;
  explicit OutputDesc(StringRef name)
      : SectionCommand(OutputSectionKind), osec(make<DataSection>(ArrayRef<OutputSegment *>())) {
    osec.name = name;
  }

  static bool classof(const SectionCommand *c) {
    return c->kind == OutputSectionKind;
  }
};

// For --sort-section and linkerscript sorting rules.
enum class SortSectionPolicy { Default, None, Alignment, Name, Priority };

// This struct represents one section match pattern in SECTIONS() command.
// It can optionally have negative match pattern for EXCLUDED_FILE command.
// Also it may be surrounded with SORT() command, so contains sorting rules.
class SectionPattern {

  // Cache of the most recent input argument and result of excludesFile().
  mutable std::optional<std::pair<const InputFile *, bool>> excludesFileCache;

public:
  SectionPattern(StringMatcher &&pat1, StringMatcher &&pat2)
      : excludedFilePat(pat1), sectionPat(pat2),
        sortOuter(SortSectionPolicy::Default),
        sortInner(SortSectionPolicy::Default) {}

  bool excludesFile(const InputFile *file) const;

  StringMatcher excludedFilePat;
  StringMatcher sectionPat;
  SortSectionPolicy sortOuter;
  SortSectionPolicy sortInner;
};

class InputSectionDescription : public SectionCommand {
  // Cache of the most recent input argument and result of matchesFile().
  mutable std::optional<std::pair<const InputFile *, bool>> matchesFileCache;

public:
  InputSectionDescription(StringRef filePattern, uint64_t withFlags = 0,
                          uint64_t withoutFlags = 0)
      : SectionCommand(InputSectionKind), filePat(filePattern),
        withFlags(withFlags), withoutFlags(withoutFlags) {}

  static bool classof(const SectionCommand *c) {
    return c->kind == InputSectionKind;
  }

  bool matchesFile(const InputFile *file) const;

  SingleStringMatcher filePat;

  // Input sections that matches at least one of SectionPatterns
  // will be associated with this InputSectionDescription.
  SmallVector<SectionPattern, 0> sectionPatterns;

  // Includes InputSections and MergeInputSections. Used temporarily during
  // assignment of input sections to output sections.
  //SmallVector<InputSectionBase *, 0> sectionBases;

  // Used after the finalizeInputSections() pass. MergeInputSections have been
  // merged into MergeSyntheticSections.
  SmallVector<InputSection *, 0> sections;

  // Temporary record of synthetic ThunkSection instances and the pass that
  // they were created in. This is used to insert newly created ThunkSections
  // into Sections at the end of a createThunks() pass.
  //SmallVector<std::pair<ThunkSection *, uint32_t>, 0> thunkSections;

  // SectionPatterns can be filtered with the INPUT_SECTION_FLAGS command.
  uint64_t withFlags;
  uint64_t withoutFlags;
};

// Represents BYTE(), SHORT(), LONG(), or QUAD().
struct ByteCommand : SectionCommand {
  ByteCommand(Expr e, unsigned size, std::string commandString)
      : SectionCommand(ByteKind), commandString(commandString), expression(e),
        size(size) {}

  static bool classof(const SectionCommand *c) { return c->kind == ByteKind; }

  // Keeps string representing the command. Used for -Map" is perhaps better.
  std::string commandString;

  Expr expression;

  // This is just an offset of this assignment command in the output section.
  unsigned offset;

  // Size of this data command.
  unsigned size;
};

class ScriptParser final : ScriptLexer {
public:
  ScriptParser(MemoryBufferRef mb) : ScriptLexer(mb) { }

  void readLinkerScript();

private:
  void readOutput();
  void readSections();

  SymbolAssignment *readSymbolAssignment(StringRef name);
  ByteCommand *readByteCommand(StringRef tok);
  std::array<uint8_t, 4> readFill();
  bool readSectionDirective(SectionBase *osec, StringRef tok1, StringRef tok2);
  void readSectionAddressType(SectionBase *osec);
  OutputDesc *readOutputSectionDescription(StringRef outSec);
  InputSectionDescription *readInputSectionDescription(StringRef tok);
  StringMatcher readFilePatterns();
  SmallVector<SectionPattern, 0> readInputSectionsList();
  InputSectionDescription *readInputSectionRules(StringRef filePattern,
                                                 uint64_t withFlags,
                                                 uint64_t withoutFlags);
  SortSectionPolicy peekSortKind();
  SortSectionPolicy readSortKind();
  SymbolAssignment *readProvideHidden(bool provide, bool hidden);
  SymbolAssignment *readAssignment(StringRef tok);
  void readSort();
  Expr readAssert();
  Expr readConstant();
  Expr getPageSize();

  Expr combine(StringRef op, Expr l, Expr r);
  Expr readExpr();
  Expr readExpr1(Expr lhs, int minPrec);
  StringRef readParenLiteral();
  Expr readPrimary();
  Expr readTernary(Expr cond);
  Expr readParenExpr();

  bool seenDataAlign = false;
  bool seenRelroEnd = false;

  // Moved from LinkerScript to here:

  OutputDesc *createOutputSection(StringRef name, StringRef location);
  OutputDesc *getOrCreateOutputSection(StringRef name);
  ExprValue getSymbolValue(StringRef name, const Twine &loc);

public:
  uint64_t dot = 0;
  //SmallVector<llvm::StringRef, 0> referencedSymbols;
  SmallVector<SectionCommand *, 0> sectionCommands;
  bool hasSectionsCommand = false;
  SmallVector<InputSectionDescription *, 0> keptSections;
  llvm::DenseMap<llvm::CachedHashStringRef, OutputDesc *> nameToOutputSection;
};

}

#endif
