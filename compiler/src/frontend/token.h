#pragma once
#include <string>
#include <unordered_map>

namespace hphl {

enum class TokenType {
  // literals & identifiers
  Identifier, IntLiteral, FloatLiteral, StringLiteral, CharLiteral,

  // keywords
  KwPublic, KwPrivate, KwProtected, KwInternal,
  KwStatic, KwInline, KwReadonly, KwConst,
  KwClass, KwStruct, KwInterface, KwEnum, KwModule, KwImport, KwUsing, KwAs,
  KwNew, KwThis, KwBase,
  KwIf, KwElse, KwSwitch, KwCase, KwDefault, KwBreak, KwContinue,
  KwFor, KwWhile, KwDo, KwForeach, KwIn, KwReturn, KwVar,
  KwTrue, KwFalse, KwNull,
  KwRef, KwOut,
  KwStack, KwHeap, KwArena, KwPool, KwShared, KwThreadLocal, KwAtomic, KwVolatile,
  KwParallel, KwSpawn, KwAsync, KwAwait, KwTask, KwChannel, KwLock, KwActor,
  KwMutex, KwSemaphore, KwEvent, KwBarrier,
  KwPanic, KwAssert, KwMatch, KwWhen, KwOk, KwErr, KwSome, KwNone,
  KwTry, KwCatch, KwThrow,
  KwDerive, KwCompiletime, KwReflect, KwSpecialize, KwWhere, KwDepends,
  KwExtern,

  // primitive types
  KwInt, KwFloat, KwDouble, KwBool, KwChar, KwString, KwPtr,
  KwU8, KwU16, KwU32, KwU64, KwVoid,

  // punctuation & operators
  LParen, RParen, LBrace, RBrace, LBracket, RBracket,
  Comma, Semicolon, Colon, Dot,
  Question, QuestionDot, QuestionBracket, QuestionQuestion, Arrow, FatArrow,
  Plus, Minus, Star, Slash, Percent, PlusPlus, MinusMinus,
  Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign, PercentAssign,
  AndAnd, OrOr, Bang, Amp, Pipe, Caret, Tilde, Shl, Shr,
  EqEq, NotEq, Lt, Gt, LtEq, GtEq,
  At, Range,

  EndOfFile, Unknown
};

struct Token {
  TokenType type = TokenType::Unknown;
  std::string text;
  long long intValue = 0;
  double floatValue = 0.0;
  int line = 0;
  int column = 0;
};

inline const std::unordered_map<std::string, TokenType>& keywordMap() {
  static const std::unordered_map<std::string, TokenType> map = {
    {"public", TokenType::KwPublic}, {"private", TokenType::KwPrivate},
    {"protected", TokenType::KwProtected}, {"internal", TokenType::KwInternal},
    {"static", TokenType::KwStatic}, {"inline", TokenType::KwInline},
    {"readonly", TokenType::KwReadonly}, {"const", TokenType::KwConst},
    {"class", TokenType::KwClass}, {"struct", TokenType::KwStruct},
    {"interface", TokenType::KwInterface}, {"enum", TokenType::KwEnum},
    {"module", TokenType::KwModule}, {"import", TokenType::KwImport},
    {"using", TokenType::KwUsing}, {"as", TokenType::KwAs}, {"new", TokenType::KwNew},
    {"this", TokenType::KwThis}, {"base", TokenType::KwBase},
    {"if", TokenType::KwIf}, {"else", TokenType::KwElse},
    {"switch", TokenType::KwSwitch}, {"case", TokenType::KwCase},
    {"default", TokenType::KwDefault}, {"break", TokenType::KwBreak},
    {"continue", TokenType::KwContinue}, {"for", TokenType::KwFor},
    {"while", TokenType::KwWhile}, {"do", TokenType::KwDo},
    {"foreach", TokenType::KwForeach}, {"in", TokenType::KwIn},
    {"ref", TokenType::KwRef}, {"out", TokenType::KwOut},
    {"return", TokenType::KwReturn}, {"var", TokenType::KwVar},
    {"true", TokenType::KwTrue}, {"false", TokenType::KwFalse},
    {"null", TokenType::KwNull},
    {"stack", TokenType::KwStack}, {"heap", TokenType::KwHeap},
    {"arena", TokenType::KwArena}, {"pool", TokenType::KwPool},
    {"shared", TokenType::KwShared}, {"threadlocal", TokenType::KwThreadLocal},
    {"atomic", TokenType::KwAtomic}, {"volatile", TokenType::KwVolatile},
    {"parallel", TokenType::KwParallel}, {"spawn", TokenType::KwSpawn},
    {"async", TokenType::KwAsync}, {"await", TokenType::KwAwait},
    {"task", TokenType::KwTask}, {"channel", TokenType::KwChannel},
    {"lock", TokenType::KwLock}, {"actor", TokenType::KwActor},
    {"mutex", TokenType::KwMutex}, {"semaphore", TokenType::KwSemaphore},
    {"event", TokenType::KwEvent}, {"barrier", TokenType::KwBarrier},
    {"panic", TokenType::KwPanic}, {"assert", TokenType::KwAssert},
    {"try", TokenType::KwTry}, {"catch", TokenType::KwCatch},
    {"throw", TokenType::KwThrow},
    {"match", TokenType::KwMatch}, {"when", TokenType::KwWhen},
    {"Ok", TokenType::KwOk}, {"Err", TokenType::KwErr},
    {"Some", TokenType::KwSome}, {"None", TokenType::KwNone},
    {"derive", TokenType::KwDerive}, {"compiletime", TokenType::KwCompiletime},
    {"reflect", TokenType::KwReflect}, {"specialize", TokenType::KwSpecialize},
    {"where", TokenType::KwWhere}, {"depends", TokenType::KwDepends},
    {"extern", TokenType::KwExtern},
    {"int", TokenType::KwInt}, {"float", TokenType::KwFloat},
    {"double", TokenType::KwDouble}, {"bool", TokenType::KwBool},
    {"char", TokenType::KwChar}, {"string", TokenType::KwString},
    {"ptr", TokenType::KwPtr},
    {"u8", TokenType::KwU8}, {"u16", TokenType::KwU16},
    {"u32", TokenType::KwU32}, {"u64", TokenType::KwU64},
    {"void", TokenType::KwVoid},
  };
  return map;
}

} // namespace hphl
