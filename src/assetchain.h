#pragma once
/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/
#include <string>

class assetchain
{
public:
    assetchain() : symbol_("") {}
    assetchain(const std::string& symbol) : symbol_(symbol)
    {
        if (symbol_.size() > 64)
            symbol_ = symbol_.substr(0, 64);
    }
    /*****
     * @returns true if the chain is Komodo
     */
    bool isKMD() { return symbol_.empty(); }
    /****
     * @param in the symbol to compare
     * @returns true if this chain's symbol matches
     */
    bool isSymbol(const std::string& in) { return in == symbol_; }
    /****
     * @returns this chain's symbol (will be empty for KMD)
     */
    std::string symbol() { return symbol_; }
    /****
     * @returns this chain's symbol, "KMD" in the case of Komodo
     */
    std::string ToString() 
    { 
        if (symbol_.empty()) 
            return "KMD"; 
        return symbol_; 
    }
    bool SymbolStartsWith(const std::string& in) { return symbol_.find(in) == 0; }
private:
    std::string symbol_;
};

extern assetchain chainName;
