/*++

Programm name:

  Apostol

Module Name:

  Exchange.cpp

Notices:

  Apostol Web Service (Exchange)

Author:

  Copyright (c) Prepodobny Alen

  mailto: alienufo@inbox.ru
  mailto: ufocomp@gmail.com

--*/

//----------------------------------------------------------------------------------------------------------------------

#include "Apostol.hpp"
#include "Exchange.hpp"
//----------------------------------------------------------------------------------------------------------------------

#include <openssl/hmac.h>
//----------------------------------------------------------------------------------------------------------------------

#define BINANCE_HOST    "https://api.binance.com"
#define POLONIEX_HOST   "https://poloniex.com"
#define BITFINEX_HOST   "https://api.bitfinex.com"
//----------------------------------------------------------------------------------------------------------------------

extern "C++" {

namespace Apostol {

    namespace Exchange {

        CString b2a_hex( char *byte_arr, int n ) {
            const static CString HexCodes = "0123456789abcdef";
            CString HexString;
            for ( int i = 0; i < n ; ++i ) {
                unsigned char BinValue = byte_arr[i];
                HexString += HexCodes[( BinValue >> 4 ) & 0x0F];
                HexString += HexCodes[BinValue & 0x0F];
            }
            return HexString;
        }

        CString hmac_sha256( const char *key, const char *data) {
            unsigned char* digest;
            digest = HMAC(EVP_sha256(), key, strlen(key), (unsigned char *) data, strlen(data), nullptr, nullptr);
            return b2a_hex( (char *) digest, 32 );
        }

        CString hmac_sha512( const char *key, const char *data) {
            unsigned char* digest;
            digest = HMAC(EVP_sha512(), key, strlen(key), (unsigned char *) data, strlen(data), nullptr, nullptr);
            return b2a_hex( (char *) digest, 32 );
        }

        CString to_string(unsigned long Value) {
            TCHAR szString[_INT_T_LEN + 1] = {0};
            sprintf(szString, "%lu", Value);
            return CString(szString);
        }

        unsigned long get_current_ms_epoch( ) {

            struct timeval tv {0};
            gettimeofday(&tv, nullptr);

            return tv.tv_sec * 1000 + tv.tv_usec / 1000;

        }

        void PairToSymbol(CExchangeType Exchange, const CString& Pair, CString& Symbol) {

            CString Str;

            size_t Pos = Pair.Find("-");

            if (Pos == CString::npos) {
                throw Delphi::Exception::ExceptionFrm(_T("Invalid format Pair: \"%s\""), Pair.c_str());
            }

            switch (Exchange) {
                case etBinance:
                    Symbol = Pair.SubString(0, Pos);
                    Symbol += Pair.SubString(Pos + 1);

                    break;

                case etPoloniex:
                    Symbol = Pair.SubString(Pos + 1);
                    Symbol += "_";
                    Symbol += Pair.SubString(0, Pos);

                    break;

                case etBitfinex:

                    Symbol = Pair.SubString(0, Pos);
                    Str = Pair.SubString(Pos + 1);

                    if (Str == "USDT") {
                        Str = "USD";
                    }

                    Symbol += Str;

                    break;

                default:
                    Symbol = Pair;
                    break;
            }
        }

        CExchange::CExchange() : CApostolModule() {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            m_curl = curl_easy_init();
            m_Exchanges = new CStringList(true);
            InitExchanges();
        }
        //--------------------------------------------------------------------------------------------------------------

        CExchange::~CExchange() {
            delete m_Exchanges;
            Cleanup();
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::Cleanup() {
            curl_easy_cleanup(m_curl);
            curl_global_cleanup();
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::InitExchanges() {

            CExchangeHandler *Handler = nullptr;
            CString ConfFileName;

            ConfFileName = Config()->ConfPrefix() + "exchange.conf";

            if (FileExists(ConfFileName.c_str())) {

                CIniFile ConfFile(ConfFileName.c_str());

                CStringList Sections;
                CStringList Section;

                ConfFile.ReadSections(&Sections);

                for (int i = 0; i < Sections.Count(); i++) {

                    ConfFile.ReadSectionValues(Sections[i].c_str(), &Section);

                    const CString &uri = Section.Values("uri");
                    const CString &ApiKey = Section.Values("ApiKey");
                    const CString &SecretKey = Section.Values("SecretKey");

                    if (Sections[i].Lower() == "binance") {
                        Handler = new CExchangeHandler(etBinance, uri.IsEmpty() ? BINANCE_HOST : uri, ApiKey, SecretKey, ConfFile.ReadBool(Sections[i].c_str(), "enabled", true));
                        Handler->OnQuoteHandlerEvent(std::bind(&CExchange::BinanceQuoteHandler, this, _1, _2, _3));
                        Handler->OnTradeHandlerEvent(std::bind(&CExchange::BinanceTradeHandler, this, _1, _2, _3));

                        m_Exchanges->AddObject(Sections[i], (CObject *) Handler);
                    }

                    if (Sections[i].Lower() == "poloniex") {
                        Handler = new CExchangeHandler(etPoloniex, uri.IsEmpty() ? POLONIEX_HOST : uri, ApiKey, SecretKey, ConfFile.ReadBool(Sections[i].c_str(), "enabled", true));
                        Handler->OnQuoteHandlerEvent(std::bind(&CExchange::PoloniexQuoteHandler, this, _1, _2, _3));
                        Handler->OnTradeHandlerEvent(std::bind(&CExchange::PoloniexTradeHandler, this, _1, _2, _3));

                        m_Exchanges->AddObject(Sections[i], (CObject *) Handler);
                    }

                    if (Sections[i].Lower() == "bitfinex") {
                        Handler = new CExchangeHandler(etBitfinex, uri.IsEmpty() ? BITFINEX_HOST : uri, ApiKey, SecretKey, ConfFile.ReadBool(Sections[i].c_str(), "enabled", true));
                        Handler->OnQuoteHandlerEvent(std::bind(&CExchange::BitfinexQuoteHandler, this, _1, _2, _3));
                        Handler->OnTradeHandlerEvent(std::bind(&CExchange::BitfinexTradeHandler, this, _1, _2, _3));

                        m_Exchanges->AddObject(Sections[i], (CObject *) Handler);
                    }
                }
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        // Curl's callback
        size_t CExchange::curl_cb(void *content, size_t size, size_t nmemb, CString *buffer)
        {
            Log()->Debug(0, "[curl_api] curl_cb");

            buffer->Append((char *) content, size * nmemb);

            //Log()->Debug(0, "[curl_api] Buffer:\n%s", buffer->c_str());

            Log()->Debug(0, "[curl_api] curl_cb: Done!");

            return size * nmemb;
        }

        // Do the curl
        void CExchange::curl_api_with_header(CString &url, CString &str_result, CStringList &extra_http_header,
                CString &post_data, CString &action) {

            Log()->Debug(0, "[curl_api] curl_api_with_header");

            CURLcode res;

            if ( m_curl ) {

                curl_easy_reset(m_curl);

                curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, CExchange::curl_cb);
                curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &str_result);
                curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, false);
                curl_easy_setopt(m_curl, CURLOPT_ENCODING, "gzip");

                if ( extra_http_header.Count() > 0 ) {

                    struct curl_slist *chunk = nullptr;
                    for ( int i = 0 ; i < extra_http_header.Count() ;i++ ) {
                        chunk = curl_slist_append(chunk, extra_http_header[i].c_str() );
                    }
                    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, chunk);
                }

                if ( action == "GET" ) {

                    curl_easy_setopt(m_curl, CURLOPT_HTTPGET, TRUE );

                } else if ( !post_data.IsEmpty() || action == "POST" || action == "PUT" || action == "DELETE" ) {

                    if ( action == "PUT" || action == "DELETE" ) {
                        curl_easy_setopt(m_curl, CURLOPT_CUSTOMREQUEST, action.c_str() );
                    }

                    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, post_data.c_str() );
                }

                res = curl_easy_perform(m_curl);

                /* Check for errors */
                if ( res != CURLE_OK ) {
                    Log()->Error(LOG_EMERG, 0, "[curl_api] curl_easy_perform() failed: %s" , curl_easy_strerror(res) ) ;
                }

            }

            Log()->Debug(0, "[curl_api] done" ) ;
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::curl_api(CString &url, CString &result_json) {
            CStringList Header;

            CString action("GET");
            CString post_data("");

            curl_api_with_header(url, result_json, Header, post_data, action);
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::GetServerTime(CString &exchange, CJSON &json_result) {

            Log()->Debug(0, "[%s] GetServerTime", exchange.c_str());

            CString url;

            if (exchange.Lower() == "binance") {
                url = BINANCE_HOST;
                url += "/api/v1/time";
            } else {
                throw Delphi::Exception::ExceptionFrm(_T("Unknown exchange: \"%s\""), exchange.c_str());
            }

            CString str_result;
            curl_api(url, str_result);

            if (!str_result.IsEmpty()) {
                try {
                    json_result.Clear();
                    json_result << str_result;
                } catch (Delphi::Exception::Exception &e) {
                    Log()->Error(LOG_EMERG, 0, "[%s] GetServerTime: Error: %s", exchange.c_str(), e.what());
                }
                Log()->Debug(0, "[%s] GetServerTime: Done!", exchange.c_str());

            } else {
                Log()->Error(LOG_EMERG, 0, "[%s] GetServerTime: Failed to get anything.", exchange.c_str());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::GetTicker(CExchangeHandler *Exchange, const CString& Symbol, CJSON &Result) {

            Log()->Debug(0, "[%s] GetTicker", Exchange->Name().c_str());

            CString url(Exchange->Uri());

            switch (Exchange->Type()) {
                case etBinance:
                    url += "/api/v3/ticker/price";

                    if (!Symbol.IsEmpty()) {
                        url += "?symbol=";
                        url += Symbol;
                    }

                    break;

                case etPoloniex:
                    url += "/public?command=returnTicker";
                    break;

                case etBitfinex:
                    url += "/v1/pubticker/";
                    url += Symbol;
                    break;
            }

            CString str_result;
            curl_api(url, str_result);

            if (!str_result.IsEmpty()) {
                try {
                    Result.Clear();
                    Result << str_result;
                } catch (Delphi::Exception::Exception &e) {
                    Log()->Error(LOG_EMERG, 0, "[%s] GetTicker: Error: %s", Exchange->Name().c_str(), e.what());
                }
                Log()->Debug(0, "[%s] GetTicker: Done!", Exchange->Name().c_str());
            } else {
                Log()->Error(LOG_EMERG, 0, "[%s] GetTicker: Failed to get anything.", Exchange->Name().c_str());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::GetTradingPairs(CHTTPConnection *AConnection) {
            auto LReply = AConnection->Reply();

            Log()->Debug(0, "GetTradingPairs");

            LReply->Content << R"(["BTC-USDT", "ETH-USDT", "XRP-USDT", "ETH-BTC", "XRP-BTC", "XRP-ETH"])";

            Log()->Debug(0, "Get: %s\n", LReply->Content.c_str());

            AConnection->SendReply(CReply::ok);

            Log()->Debug(0, "GetTradingPairs: Done!");
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::BinanceQuoteHandler(CExchangeHandler *Exchange, const CStringList &Params, CString &Result) {

            Log()->Debug(0, "[%s] Order book", Exchange->Name().c_str());

            CString url(Exchange->Uri());
            url += "/api/v1/depth?";

            CString Symbol;
            PairToSymbol(Exchange->Type(), Params["pair"], Symbol);

            CString QueryString("symbol=");
            QueryString.Append(Symbol);

            QueryString.Append("&limit=1000");

            url.Append(QueryString);

            Log()->Debug(0, "[%s] Order book: uri = |%s|", Exchange->Name().c_str(), url.c_str());

            curl_api(url, Result);

            if (!Result.IsEmpty()) {
                Log()->Debug(0, "[%s] Order book: Done.", Exchange->Name().c_str());
            } else {
                Log()->Debug(0, "[%s] Order book: Failed to get anything", Exchange->Name().c_str());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::BinanceTradeHandler(CExchangeHandler *Exchange, const CStringList &Params, CString &Result) {

            Log()->Debug(0, "[%s] Send Order", Exchange->Name().c_str());

            if (Exchange->ApiKey().IsEmpty()) {
                throw Delphi::Exception::ExceptionFrm(_T("[%s] API Key has not been set"), Exchange->Name().c_str());
            }

            if (Exchange->SecretKey().IsEmpty()) {
                throw Delphi::Exception::ExceptionFrm(_T("[%s] Secret Key has not been set"), Exchange->Name().c_str());
            }

            CString Symbol;
            PairToSymbol(Exchange->Type(), Params["pair"], Symbol);

            CString action = "POST";

            CString url(Exchange->Uri());
            url += "/api/v3/order?";

            CString post_data("symbol=");
            post_data.Append( Symbol );

            post_data.Append("&side=");
            post_data.Append( Params["type"] );

            post_data.Append("&type=");
            post_data.Append( "MARKET" );

            post_data.Append("&timeInForce=");
            post_data.Append( "GTC" );

            post_data.Append("&quantity=");
            post_data.Append( Params["amount"] );

            const CString& price = Params["price"];

            if ( !price.IsEmpty() && price != "0" ) {
                post_data.Append("&price=");
                post_data.Append( price );
            }

            const CString& newClientOrderId = Params["newClientOrderId"];

            if ( !newClientOrderId.IsEmpty() ) {
                post_data.Append("&newClientOrderId=");
                post_data.Append( newClientOrderId );
            }

            const CString& stopPrice = Params["stopPrice"];

            if ( !stopPrice.IsEmpty() && stopPrice != "0.0" ) {
                post_data.Append("&stopPrice=");
                post_data.Append( stopPrice );
            }

            const CString& icebergQty = Params["icebergQty"];

            if ( !icebergQty.IsEmpty() && icebergQty != "0.0" ) {
                post_data.Append("&icebergQty=");
                post_data.Append( icebergQty );
            }

            const CString& recvWindow = Params["recvWindow"];

            if ( !recvWindow.IsEmpty() && recvWindow != "0" ) {
                post_data.Append("&recvWindow=");
                post_data.Append( recvWindow );
            }

            post_data.Append("&timestamp=");
            post_data.Append( to_string( get_current_ms_epoch() ) );

            CString signature = hmac_sha256( Exchange->SecretKey().c_str(), post_data.c_str() );

            post_data.Append( "&signature=");
            post_data.Append( signature );

            CStringList extra_http_header;
            CString header_chunk("X-MBX-APIKEY: ");
            header_chunk.Append( Exchange->ApiKey() );
            extra_http_header.Add(header_chunk);

            Log()->Debug(0, "[%s] Send Order: uri = |%s%s|", Exchange->Name().c_str(), url.c_str(), post_data.c_str());

            curl_api_with_header( url, Result, extra_http_header, post_data, action ) ;

            if (!Result.IsEmpty()) {
                Log()->Debug(0, "[%s] Send order: Done.", Exchange->Name().c_str());
            } else {
                Log()->Debug(0, "[%s] Send order: Failed to get anything", Exchange->Name().c_str());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::PoloniexQuoteHandler(CExchangeHandler *Exchange, const CStringList &Params, CString &Result) {

            Log()->Debug(0, "[%s] Order book", Exchange->Name().c_str());

            CString url(Exchange->Uri());
            url += "/public?";

            CString QueryString("command=returnOrderBook&currencyPair=");

            CString Symbol;
            PairToSymbol(Exchange->Type(), Params["pair"], Symbol);

            QueryString.Append(Symbol);
            QueryString.Append("&depth=100"); // Max: 100

            url.Append(QueryString);

            Log()->Debug(0, "[%s] Order book: uri = |%s|", Exchange->Name().c_str(), url.c_str());

            curl_api(url, Result);

            if (!Result.IsEmpty()) {
                Log()->Debug(0, "[%s] Order book: Done.", Exchange->Name().c_str());
            } else {
                Log()->Debug(0, "[%s] Order book: Failed to get anything", Exchange->Name().c_str());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::PoloniexTradeHandler(CExchangeHandler *Exchange, const CStringList &Params, CString &Result) {

            Log()->Debug(0, "[%s] Send Order", Exchange->Name().c_str());

            if (Exchange->ApiKey().IsEmpty()) {
                throw Delphi::Exception::ExceptionFrm(_T("[%s] API Key has not been set"), Exchange->Name().c_str());
            }

            if (Exchange->SecretKey().IsEmpty()) {
                throw Delphi::Exception::ExceptionFrm(_T("[%s] Secret Key has not been set"), Exchange->Name().c_str());
            }

            CString action = "POST";

            CString Symbol;
            PairToSymbol(Exchange->Type(), Params["pair"], Symbol);

            CString Rate(Params["rate"]);

            if (Rate.IsEmpty()) {
                CJSON Json;
                GetTicker(Exchange, Symbol, Json);
                Rate = Json[Symbol]["last"].AsSiring();
            }

            CString url(Exchange->Uri());
            url += "/tradingApi?";

            CString post_data("command=");
            post_data.Append( Params["type"].Lower() );

            post_data.Append("&currencyPair=");
            post_data.Append( Symbol );

            post_data.Append("&rate=");
            post_data.Append( Rate );

            post_data.Append("&amount=");
            post_data.Append( Params["amount"] );

            const CString& fillOrKill = Params["fillOrKill"];

            if ( !fillOrKill.IsEmpty() && fillOrKill != "0" ) {
                post_data.Append("&fillOrKill=");
                post_data.Append( fillOrKill );
            }

            const CString& immediateOrCancel = Params["immediateOrCancel"];

            if ( !immediateOrCancel.IsEmpty() && immediateOrCancel != "0" ) {
                post_data.Append("&immediateOrCancel=");
                post_data.Append( immediateOrCancel );
            }

            const CString& postOnly = Params["postOnly"];

            if ( !postOnly.IsEmpty() && postOnly != "0" ) {
                post_data.Append("&postOnly=");
                post_data.Append( postOnly );
            }

            post_data.Append("&nonce=");
            post_data.Append( to_string( get_current_ms_epoch() ) );

            CString signature = hmac_sha512( Exchange->SecretKey().c_str(), post_data.c_str() );

            CStringList extra_http_header;

            CString header_key("Key: ");
            header_key.Append( Exchange->ApiKey() );

            CString header_sign("Sign: ");
            header_sign.Append( signature );

            extra_http_header.Add(header_key);
            extra_http_header.Add(header_sign);

            Log()->Debug(0, "[%s] Send Order: uri = |%s%s|", Exchange->Name().c_str(), url.c_str(), post_data.c_str());

            curl_api_with_header( url, Result, extra_http_header, post_data, action ) ;

            if (!Result.IsEmpty()) {
                Log()->Debug(0, "[%s] Send order: Done.", Exchange->Name().c_str());
            } else {
                Log()->Debug(0, "[%s] Send order: Failed to get anything", Exchange->Name().c_str());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::BitfinexQuoteHandler(CExchangeHandler *Exchange, const CStringList &Params, CString &Result) {

            Log()->Debug(0, "[%s] Order book", Exchange->Name().c_str());

            CString url(Exchange->Uri());
            url += "/v1/book/";

            CString Symbol;
            PairToSymbol(Exchange->Type(), Params["pair"], Symbol);

            url.Append(Symbol.Lower());

            Log()->Debug(0, "[%s] Order book: uri = |%s|", Exchange->Name().c_str(), url.c_str());

            curl_api(url, Result);

            if (!Result.IsEmpty()) {
                Log()->Debug(0, "[%s] Order book: Done.", Exchange->Name().c_str());
            } else {
                Log()->Debug(0, "[%s] Order book: Failed to get anything", Exchange->Name().c_str());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::BitfinexTradeHandler(CExchangeHandler *Exchange, const CStringList &Params, CString &Result) {

        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::Quote(const CStringList& Params, CHTTPConnection *AConnection) {

            auto LReply = AConnection->Reply();

            const CString &Type = Params.Values("type");
            const CString &Amount = Params.Values("amount");

            double amount = StrToFloat(Amount.c_str());

            TList<CJSON> Json;

            Log()->Debug(0, "Quote");

            CString asks_bids;
            if (Type == "BUY") {
                asks_bids = "asks";
            } else if (Type == "SELL") {
                asks_bids = "bids";
            }

            CStringList Results;

            int Count = 0;

            LReply->Content = "[";

            CExchangeHandler *Handler;
            // Перебор бирж...
            for (int i = 0; i < m_Exchanges->Count(); i++) {

                Handler = (CExchangeHandler *) m_Exchanges->Objects(i);
                Json.Add(CJSON());

                if (Handler->Enabled()) {

                    Results.Add(CString());
                    Handler->QuoteHandler(Handler, Params, Results[i]); // Запрос данных с биржи...
                    Json[i] << Results[i]; // Конвертируем в JSON

                    const CJSONValue &Data = Json[i][asks_bids];

                    if (Data.IsArray()) {

                        double totalAmountCurrency = 0;
                        double totalAmountAsset = amount;

                        for (int j = 0; j < Data.Count(); j++) {

                            const CJSONValue &Next = Data[j];

                            if (totalAmountAsset > Next[1].AsDouble()) {
                                totalAmountAsset -= Next[1].AsDouble();
                                totalAmountCurrency += Next[0].AsDouble() * Next[1].AsDouble();
                            } else {
                                totalAmountCurrency += Next[0].AsDouble() * totalAmountAsset;
                                totalAmountAsset = 0;
                                break;
                            }
                        }

                        if (totalAmountCurrency != 0) {

                            if (Count > 0) {
                                LReply->Content += ", ";
                            }

                            CString Str;
                            Str.Format(
                                    _T("{\"asks_bids\": \"%s\", \"AveragePrice\": %f, \"TotalPrice\": %f, \"Exchange\": \"%s\", \"Remain\": %f}"),
                                    asks_bids.c_str(),
                                    totalAmountCurrency / (amount - totalAmountAsset),
                                    totalAmountCurrency,
                                    Handler->Name().c_str(),
                                    totalAmountAsset);

                            LReply->Content += Str;

                            Count++;
                        }
                    }
                }
            }

            LReply->Content += "]";

            Log()->Debug(0, "Get: %s\n", LReply->Content.c_str());

            AConnection->SendReply(CReply::ok);

            Log()->Debug(0, "Quote: Done!");
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::Trade(const CStringList &Params, CHTTPConnection *AConnection) {

            auto LReply = AConnection->Reply();

            Log()->Debug(0, "Trade");

            CExchangeHandler *Handler;
            // Перебор бирж...
            for (int i = 0; i < m_Exchanges->Count(); i++) {

                Handler = (CExchangeHandler *) m_Exchanges->Objects(i);

                if (Handler->Enabled() && (Params["exchange"].Lower() == Handler->Name().Lower())) {
                    Handler->TradeHandler(Handler, Params, LReply->Content); // Отправляем ордер...
                }
            }

            if (!LReply->Content.IsEmpty()) {
                Log()->Debug(0, "Get: %s\n", LReply->Content.c_str());
            }

            AConnection->SendReply(CReply::ok);

            Log()->Debug(0, "Trade: Done!");
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::ExceptionToJson(Delphi::Exception::Exception *AException, CString &Json) {
            Json.Format(
                    _T("{\"error\": {\"errors\": [{\"domain\": \"%s\", \"reason\": \"%s\", \"message\": "
                       "\"%s\", \"locationType\": \"%s\", \"location\": \"%s\"}], \"code\": %u, "
                       "\"message\": \"%s\"}}"), _T("module"), _T("exception"),
                    AException->what(), _T("cURL"), _T("Exchange"), 500, _T("Internal Server Error"));
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::Post(CHTTPConnection *AConnection) {

            auto LRequest = AConnection->Request();
            int LVersion = -1;

            CStringList LUri;
            SplitColumns(LRequest->Uri.c_str(), LRequest->Uri.Size(), &LUri, '/');
            if (LUri.Count() < 3) {
                AConnection->SendStockReply(CReply::not_found);
                return;
            }

            if (LUri[1] == _T("v1")) {
                LVersion = 1;
            }

            if (LUri[0] != _T("api") || (LVersion == -1)) {
                AConnection->SendStockReply(CReply::not_found);
                return;
            }

            auto LReply = AConnection->Reply();

            try {

                if (LUri[2] == _T("Trade")) {

                    if (LUri.Count() == 7) {
                        CStringList Params;

                        Params.AddPair("pair", LUri[3]);
                        Params.AddPair("type", LUri[4]);
                        Params.AddPair("amount", LUri[5]);
                        Params.AddPair("exchange", LUri[6]);

                        // Poloniex default value
                        Params.AddPair("immediateOrCancel", "1");

                        Trade(Params, AConnection);

                        return;
                    }
                }

                AConnection->SendStockReply(CReply::not_found);

            } catch (Delphi::Exception::Exception &E) {
                CReply::status_type LStatus = CReply::internal_server_error;

                ExceptionToJson(&E, LReply->Content);

                AConnection->SendReply(LStatus);
                Log()->Error(LOG_EMERG, 0, E.what());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::Get(CHTTPConnection *AConnection) {

            auto LRequest = AConnection->Request();
            int LVersion = -1;

            CStringList LUri;
            SplitColumns(LRequest->Uri.c_str(), LRequest->Uri.Size(), &LUri, '/');
            if (LUri.Count() < 3) {
                AConnection->SendStockReply(CReply::not_found);
                return;
            }

            if (LUri[1] == _T("v1")) {
                LVersion = 1;
            }

            if (LUri[0] != _T("api") || (LVersion == -1)) {
                AConnection->SendStockReply(CReply::not_found);
                return;
            }

            auto LReply = AConnection->Reply();

            try {

                if (LUri[2] == _T("TradingPairs")) {

                    GetTradingPairs(AConnection);
                    return;

                } else if (LUri[2] == _T("Quote")) {

                    if (LUri.Count() == 6) {
                        CStringList Params;

                        Params.AddPair("pair", LUri[3]);
                        Params.AddPair("type", LUri[4]);
                        Params.AddPair("amount", LUri[5]);

                        Quote(Params, AConnection);

                        return;
                    }
                }

                AConnection->SendStockReply(CReply::not_found);

            } catch (Delphi::Exception::Exception &E) {
                CReply::status_type LStatus = CReply::internal_server_error;

                ExceptionToJson(&E, LReply->Content);

                AConnection->SendReply(LStatus);
                Log()->Error(LOG_EMERG, 0, E.what());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::DoPostgresQueryExecuted(CPQPollQuery *APollQuery) {

        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::DoPostgresQueryException(CPQPollQuery *APollQuery, Delphi::Exception::Exception *AException) {

        }
        //--------------------------------------------------------------------------------------------------------------

        void CExchange::Execute(CHTTPConnection *AConnection) {

            auto LRequest = AConnection->Request();
            auto LReply = AConnection->Reply();

            LReply->Clear();
            LReply->ContentType = CReply::json;

            if (LRequest->Method == _T("POST")) {
                Post(AConnection);
                return;
            }

            if (LRequest->Method == _T("GET")) {
                Get(AConnection);
                return;
            }

            AConnection->SendStockReply(CReply::not_implemented);
        }

    }

}
}