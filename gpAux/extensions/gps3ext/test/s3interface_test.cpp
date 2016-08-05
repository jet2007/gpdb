#include "s3interface.cpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mock_classes.h"

using ::testing::AtLeast;
using ::testing::Return;
using ::testing::Throw;
using ::testing::_;

class S3ServiceTest : public testing::Test {
   protected:
    // Remember that SetUp() is run immediately before a test starts.
    virtual void SetUp() {
        result = NULL;

        s3ext_logtype = STDERR_LOG;
        s3ext_loglevel = EXT_INFO;

        s3service = new S3Service();
        schema = "https";
        s3service->setRESTfulService(&mockRestfulService);
    }

    // TearDown() is invoked immediately after a test finishes.
    virtual void TearDown() {
        delete s3service;

        if (result != NULL) {
            delete result;
            result = NULL;
        }
    }

    Response buildListBucketResponse(int numOfContent, bool isTruncated, int numOfZeroKeys = 0) {
        XMLGenerator generator;
        XMLGenerator *gen = &generator;
        gen->setName("s3test.pivotal.io")
            ->setPrefix("s3files/")
            ->setIsTruncated(isTruncated)
            ->pushBuckentContent(BucketContent("s3files/", 0));

        char buffer[32] = {0};
        for (int i = 0; i < numOfContent; ++i) {
            snprintf(buffer, 32, "files%d", i);
            gen->pushBuckentContent(BucketContent(buffer, i + 1));
        }

        for (int i = 0; i < numOfZeroKeys; i++) {
            snprintf(buffer, 32, "zerofiles%d", i);
            gen->pushBuckentContent(BucketContent(buffer, 0));
        }

        return Response(RESPONSE_OK, gen->toXML());
    }

    S3Service *s3service;
    S3Credential cred;
    string schema;
    string region;
    string bucket;
    string prefix;

    MockS3RESTfulService mockRestfulService;
    Response response;

    ListBucketResult *result;
};

TEST_F(S3ServiceTest, GetResponseWithZeroRetry) {
    string url = "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever";
    HTTPHeaders headers;
    map<string, string> params;
    EXPECT_EQ(RESPONSE_FAIL,
              s3service->getResponseWithRetries(url, headers, params, 0).getStatus());
}

TEST_F(S3ServiceTest, GetResponseWithTwoRetries) {
    string url = "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever";
    HTTPHeaders headers;
    map<string, string> params;

    EXPECT_CALL(mockRestfulService, get(_, _, _))
        .Times(2)
        .WillOnce(Return(response))
        .WillOnce(Return(response));

    EXPECT_EQ(RESPONSE_FAIL,
              s3service->getResponseWithRetries(url, headers, params, 2).getStatus());
}

TEST_F(S3ServiceTest, ListBucketThrowExceptionWhenBucketStringIsEmpty) {
    // here is a memory leak because we haven't defined ~S3ServiceTest() to clean up.
    EXPECT_THROW(result = s3service->listBucket("", "", "", "", cred), std::runtime_error);
}

TEST_F(S3ServiceTest, ListBucketWithWrongRegion) {
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    EXPECT_EQ((void *)NULL, result = s3service->listBucket(schema, "nonexist", "", "", cred));
}

TEST_F(S3ServiceTest, ListBucketWithWrongBucketName) {
    uint8_t xml[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Error>"
        "<Code>PermanentRedirect</Code>"
        "<Message>The bucket you are attempting to access must be addressed "
        "using the specified endpoint. "
        "Please send all future requests to this endpoint.</Message>"
        "<Bucket>foo</Bucket><Endpoint>s3.amazonaws.com</Endpoint>"
        "<RequestId>27DD9B7004AF83E3</RequestId>"
        "<HostId>NL3pyGvn+FajhQLKz/"
        "hXUzV1VnFbbwNjUQsqWeFiDANkV4EVkh8Kpq5NNAi27P7XDhoA9M9Xhg0=</HostId>"
        "</Error>";
    vector<uint8_t> raw(xml, xml + sizeof(xml) - 1);
    Response response(RESPONSE_ERROR, raw);

    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    EXPECT_EQ((void *)NULL,
              result = s3service->listBucket(schema, "us-west-2", "foo/bar", "", cred));
}

TEST_F(S3ServiceTest, ListBucketWithNormalBucket) {
    XMLGenerator generator;
    XMLGenerator *gen = &generator;
    gen->setName("s3test.pivotal.io")
        ->setPrefix("threebytes/")
        ->setIsTruncated(false)
        ->pushBuckentContent(BucketContent("threebytes/", 0))
        ->pushBuckentContent(BucketContent("threebytes/threebytes", 3));

    Response response(RESPONSE_OK, gen->toXML());

    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillOnce(Return(response));

    result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io", "threebytes/", cred);
    ASSERT_NE((void *)NULL, result);
    EXPECT_EQ(1, result->contents.size());
}

TEST_F(S3ServiceTest, ListBucketWithBucketWith1000Keys) {
    EXPECT_CALL(mockRestfulService, get(_, _, _))
        .WillOnce(Return(this->buildListBucketResponse(1000, false)));

    result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io", "s3files/", cred);
    ASSERT_NE((void *)NULL, result);
    EXPECT_EQ(1000, result->contents.size());
}

TEST_F(S3ServiceTest, ListBucketWithBucketWith1001Keys) {
    EXPECT_CALL(mockRestfulService, get(_, _, _))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillOnce(Return(this->buildListBucketResponse(1, false)));

    result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io", "s3files/", cred);
    ASSERT_NE((void *)NULL, result);
    EXPECT_EQ(1001, result->contents.size());
}

TEST_F(S3ServiceTest, ListBucketWithBucketWithMoreThan1000Keys) {
    EXPECT_CALL(mockRestfulService, get(_, _, _))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillOnce(Return(this->buildListBucketResponse(120, false)));

    result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io", "s3files/", cred);
    ASSERT_NE((void *)NULL, result);
    EXPECT_EQ(5120, result->contents.size());
}

TEST_F(S3ServiceTest, ListBucketWithBucketWithTruncatedResponse) {
    Response EmptyResponse;

    EXPECT_CALL(mockRestfulService, get(_, _, _))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillRepeatedly(Return(EmptyResponse));

    result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io", "s3files/", cred);
    EXPECT_EQ((void *)NULL, result);
}

TEST_F(S3ServiceTest, ListBucketWithBucketWithZeroSizedKeys) {
    EXPECT_CALL(mockRestfulService, get(_, _, _))
        .WillOnce(Return(this->buildListBucketResponse(0, true, 8)))
        .WillOnce(Return(this->buildListBucketResponse(1000, true)))
        .WillOnce(Return(this->buildListBucketResponse(120, false, 8)));

    result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io", "s3files/", cred);
    ASSERT_NE((void *)NULL, result);
    EXPECT_EQ(1120, result->contents.size());
}

TEST_F(S3ServiceTest, ListBucketWithEmptyBucket) {
    EXPECT_CALL(mockRestfulService, get(_, _, _))
        .WillOnce(Return(this->buildListBucketResponse(0, false, 0)));

    result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io", "s3files/", cred);
    ASSERT_NE((void *)NULL, result);
    EXPECT_EQ(0, result->contents.size());
}

TEST_F(S3ServiceTest, ListBucketWithAllZeroedFilesBucket) {
    EXPECT_CALL(mockRestfulService, get(_, _, _))
        .WillOnce(Return(this->buildListBucketResponse(0, false, 2)));

    result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io", "s3files/", cred);
    ASSERT_NE((void *)NULL, result);
    EXPECT_EQ(0, result->contents.size());
}

TEST_F(S3ServiceTest, ListBucketWithErrorResponse) {
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    EXPECT_EQ((void *)NULL, result = s3service->listBucket(schema, "nonexist", "s3test.pivotal.io",
                                                           "s3files/", cred));
}

TEST_F(S3ServiceTest, ListBucketWithErrorReturnedXML) {
    uint8_t xml[] = "whatever";
    vector<uint8_t> raw(xml, xml + sizeof(xml) - 1);
    Response response(RESPONSE_ERROR, raw);

    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    EXPECT_EQ((void *)NULL, result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io",
                                                           "s3files/", cred));
}

TEST_F(S3ServiceTest, ListBucketWithNonRootXML) {
    uint8_t xml[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    vector<uint8_t> raw(xml, xml + sizeof(xml) - 1);
    Response response(RESPONSE_ERROR, raw);

    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    EXPECT_EQ((void *)NULL, result = s3service->listBucket(schema, "us-west-2", "s3test.pivotal.io",
                                                           "s3files/", cred));
}

TEST_F(S3ServiceTest, fetchDataRoutine) {
    vector<uint8_t> raw;

    srand(time(NULL));

    for (int i = 0; i < 100; i++) {
        raw.push_back(rand() & 0xFF);
    }

    Response response(RESPONSE_OK, raw);
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillOnce(Return(response));

    vector<uint8_t> buffer;

    uint64_t len = s3service->fetchData(
        0, buffer, 100, "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region,
        cred);

    EXPECT_EQ(buffer.size(), raw.size());
    EXPECT_EQ(0, memcmp(buffer.data(), raw.data(), 100));
    EXPECT_EQ(100, len);
}

TEST_F(S3ServiceTest, fetchDataErrorResponse) {
    vector<uint8_t> raw;
    raw.resize(100);
    Response response(RESPONSE_ERROR, raw);
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    vector<uint8_t> buffer;

    EXPECT_THROW(s3service->fetchData(
                     0, buffer, 100,
                     "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region, cred),
                 std::runtime_error);
}

TEST_F(S3ServiceTest, fetchDataFailedResponse) {
    vector<uint8_t> raw;
    raw.resize(100);
    Response response(RESPONSE_FAIL, raw);
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    vector<uint8_t> buffer;

    EXPECT_THROW(s3service->fetchData(
                     0, buffer, 100,
                     "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region, cred),
                 std::runtime_error);
}

TEST_F(S3ServiceTest, fetchDataPartialResponse) {
    vector<uint8_t> raw;
    raw.resize(80);
    Response response(RESPONSE_OK, raw);
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillOnce(Return(response));
    vector<uint8_t> buffer;

    EXPECT_THROW(s3service->fetchData(
                     0, buffer, 100,
                     "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region, cred),
                 std::runtime_error);
}

TEST_F(S3ServiceTest, checkSmallFile) {
    vector<uint8_t> raw;
    raw.resize(2);
    raw[0] = 0x1f;
    raw[1] = 0x8b;
    Response response(RESPONSE_OK, raw);
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillOnce(Return(response));

    EXPECT_EQ(S3_COMPRESSION_PLAIN,
              s3service->checkCompressionType(
                  "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region, cred));
}

TEST_F(S3ServiceTest, checkItsGzipCompressed) {
    vector<uint8_t> raw;
    raw.resize(4);
    raw[0] = 0x1f;
    raw[1] = 0x8b;
    Response response(RESPONSE_OK, raw);
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillOnce(Return(response));

    EXPECT_EQ(S3_COMPRESSION_GZIP,
              s3service->checkCompressionType(
                  "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region, cred));
}

TEST_F(S3ServiceTest, checkItsNotCompressed) {
    vector<uint8_t> raw;
    raw.resize(4);
    raw[0] = 0x1f;
    raw[1] = 0x88;
    Response response(RESPONSE_OK, raw);
    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillOnce(Return(response));

    EXPECT_EQ(S3_COMPRESSION_PLAIN,
              s3service->checkCompressionType(
                  "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region, cred));
}

TEST_F(S3ServiceTest, checkCompreesionTypeWithResponseError) {
    uint8_t xml[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Error>"
        "<Code>PermanentRedirect</Code>"
        "<Message>The bucket you are attempting to access must be addressed "
        "using the specified endpoint. "
        "Please send all future requests to this endpoint.</Message>"
        "<Bucket>foo</Bucket><Endpoint>s3.amazonaws.com</Endpoint>"
        "<RequestId>27DD9B7004AF83E3</RequestId>"
        "<HostId>NL3pyGvn+FajhQLKz/"
        "hXUzV1VnFbbwNjUQsqWeFiDANkV4EVkh8Kpq5NNAi27P7XDhoA9M9Xhg0=</HostId>"
        "</Error>";
    vector<uint8_t> raw(xml, xml + sizeof(xml) - 1);
    Response response(RESPONSE_ERROR, raw);

    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    EXPECT_THROW(s3service->checkCompressionType(
                     "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region, cred),
                 std::runtime_error);
}

TEST_F(S3ServiceTest, fetchDataWithResponseError) {
    uint8_t xml[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Error>"
        "<Code>PermanentRedirect</Code>"
        "<Message>The bucket you are attempting to access must be addressed "
        "using the specified endpoint. "
        "Please send all future requests to this endpoint.</Message>"
        "<Bucket>foo</Bucket><Endpoint>s3.amazonaws.com</Endpoint>"
        "<RequestId>27DD9B7004AF83E3</RequestId>"
        "<HostId>NL3pyGvn+FajhQLKz/"
        "hXUzV1VnFbbwNjUQsqWeFiDANkV4EVkh8Kpq5NNAi27P7XDhoA9M9Xhg0=</HostId>"
        "</Error>";
    vector<uint8_t> raw(xml, xml + sizeof(xml) - 1);
    Response response(RESPONSE_ERROR, raw);
    vector<uint8_t> buffer;

    EXPECT_CALL(mockRestfulService, get(_, _, _)).WillRepeatedly(Return(response));

    EXPECT_THROW(s3service->fetchData(
                     0, buffer, 128,
                     "https://s3-us-west-2.amazonaws.com/s3test.pivotal.io/whatever", region, cred),
                 std::runtime_error);
}
