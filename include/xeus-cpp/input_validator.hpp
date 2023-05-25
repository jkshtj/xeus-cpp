/************************************************************************************
 * Copyright (c) 2023, xeus-cpp contributors                                        *
 * Copyright (c) 2023, Johan Mabille, Loic Gouarin, Sylvain Corlay, Wolf Vollprecht *
 *                                                                                  *
 * Distributed under the terms of the BSD 3-Clause License.                         *
  *                                                                                 *
 * The full license is in the file LICENSE, distributed with this software.         *
 ************************************************************************************/

#ifndef XEUS_CPP_INPUT_VALIDATOR_H
#define XEUS_CPP_INPUT_VALIDATOR_H

#include <clang/Lex/Lexer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/ADT/StringRef.h>
#include <stack>
#include <memory>

namespace xpp {
    class input_validator {
    public:
        enum class ValidationResult {
            complete,
            incomplete, 
            invalid, 
            unknown
        };

        ValidationResult validate(const std::string& code, const clang::CompilerInstance* compiler) {
            // Set up and initialize the preprocessor
            clang::SourceManager& sourceManager = compiler->getSourceManager();
            std::unique_ptr<llvm::MemoryBuffer> memBuffer = llvm::MemoryBuffer::getMemBuffer(code);
            llvm::MemoryBufferRef memBufferRef = memBuffer->getMemBufferRef();
            clang::FileID fileId = sourceManager.createFileID(memBufferRef);
            clang::Preprocessor& preprocessor = compiler->getPreprocessor();
            preprocessor.EnterSourceFile(fileId, nullptr, clang::SourceLocation());
            
            ValidationResult result = ValidationResult::complete;
            clang::Token token, lastSeenToken;

            do {
                lastSeenToken = token;
                preprocessor.Lex(token);
                
                switch (token.getKind()) {
                    default:
                        break;
                    case clang::tok::l_square: case clang::tok::l_brace: case clang::tok::l_paren: {
                        parenStack.push_back(token.getKind());
                        break;
                    }
                    case clang::tok::r_square: case clang::tok::r_brace: case clang::tok::r_paren: {
                        auto tos = parenStack.empty()
                            ? clang::tok::unknown : static_cast<clang::tok::TokenKind>(parenStack.back());
                        if (!(token.getKind() == tos+1)) {
                            result = ValidationResult::invalid;
                            break;
                        }
                        parenStack.pop_back();
                        
                        // '}' will also pop a template '<' if their is one
                        if (token.getKind() == clang::tok::r_brace && parenStack.size() == 1
                            && parenStack.back() == clang::tok::less)
                            parenStack.pop_back();
                        
                        break;
                    }
                    case clang::tok::hash: {
                        preprocessor.Lex(token);
                        llvm::StringRef tokenText = llvm::StringRef(sourceManager.getCharacterData(token.getLocation()), token.getLength());
                        if (tokenText.startswith("if")) {
                            parenStack.push_back(clang::tok::hash);
                        } else if (tokenText.startswith("endif") &&
                                (tokenText.size() == 5 || tokenText[5]=='/' || isspace(tokenText[5]))) {
                            if (parenStack.empty() || parenStack.back() != clang::tok::hash)
                                result = ValidationResult::invalid;
                            else
                                parenStack.pop_back();
                        }
                        break;        
                    }
                }
            } while (token.isNot(clang::tok::eof) && result != ValidationResult::invalid);

            const bool should_continue = lastSeenToken.getKind() == clang::tok::comma;
            // || (lastSeenToken.getKind() == clang::tok::backslash;
            
            if (should_continue || (!parenStack.empty() && result != ValidationResult::invalid)) {
                result = ValidationResult::incomplete;
            }

            if (!input.empty()) {
                input.append("\n");
            }
            input.append(code);
            lastResult = result;

            return result;
        }


        void reset(std::string* code) {
            if (code) {
                assert(code->empty() && "InputValidator::reset got non empty argument");
                code->swap(input);
            } else {
                std::string().swap(input);
            }

            std::deque<int>().swap(parenStack);
            lastResult = ValidationResult::complete;
        }
    
    private:
        ///\brief The input being collected.
        ///
        std::string input;

        ///\brief Stack used for checking the brace balance.
        ///
        std::deque<int> parenStack;

        ///\brief Last validation result from `validate()`.
        ///
        ValidationResult lastResult = ValidationResult::complete;

        bool rightClosesLeftBrace(const clang::Token& r_brace, const clang::tok::TokenKind l_brace_tok_kind) {
            switch (r_brace.getKind()) {
                case clang::tok::r_square: case clang::tok::r_brace: case clang::tok::r_paren:
                    return r_brace.getKind() == l_brace_tok_kind+1;
                default:
                    return false;
            }
        }
    };
}

#endif