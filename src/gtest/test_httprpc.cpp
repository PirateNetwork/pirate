#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "httprpc.cpp"
#include "httpserver.h"

using ::testing::Return;

class MockHTTPRequest : public HTTPRequest {
public:
    MOCK_METHOD0(GetPeer, CService());
    MOCK_METHOD0(GetRequestMethod, HTTPRequest::RequestMethod());
    MOCK_METHOD1(GetHeader, std::pair<bool, std::string>(const std::string& hdr));
    MOCK_METHOD2(WriteHeader, void(const std::string& hdr, const std::string& value));
    MOCK_METHOD2(WriteReply, void(int nStatus, const std::string& strReply));

    MockHTTPRequest() : HTTPRequest(nullptr) {}
    void CleanUp() {
        // So the parent destructor doesn't try to send a reply
        replySent = true;
    }
};

TEST(HTTPRPC, FailsOnGET) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::GET));
    EXPECT_CALL(req, WriteReply(HTTP_BAD_METHOD, "JSONRPC server handles only POST requests"))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

TEST(HTTPRPC, FailsWithoutAuthHeader) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(false, "")));
    EXPECT_CALL(req, WriteHeader("WWW-Authenticate", "Basic realm=\"jsonrpc\""))
        .Times(1);
    EXPECT_CALL(req, WriteReply(HTTP_UNAUTHORIZED, ""))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

TEST_F(HTTPRPC, FailsWithBadAuth)
{
    // Mock the getpeerinfo RPC call to succeed, so that a username and password
    // for the remote peer is added to the rpcauth table.
    EXPECT_CALL(rpcService, CallRPC("getpeerinfo", _, _))
        .WillOnce(Return(UniValue(UniValue::VARR)));
    // Mock the lookup function to return a CService.
    // This is necessary because the default mock action for LookupNumeric is to return false.
    EXPECT_CALL(*pLookupNumericMock, LookupNumeric("127.0.0.1", _, _))
        .WillRepeatedly(Return(CService(CNetAddr("127.0.0.1"), 1337)));

    // Test the HTTP basic authentication.
    // Wrong password
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(true, "Basic spam:eggs")));
    EXPECT_CALL(req, GetPeer())
        .WillRepeatedly(Return(CService("127.0.0.1:1337")));
    EXPECT_CALL(req, WriteHeader("WWW-Authenticate", "Basic realm=\"jsonrpc\""))
        .Times(1);
    EXPECT_CALL(req, WriteReply(HTTP_UNAUTHORIZED, ""))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}
