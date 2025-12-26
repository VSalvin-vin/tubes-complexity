#include "api_handler.h"
#include "search_engine.h"
#include <iostream>
#include <vector>
#include <set>
#include <sstream>
#include <fstream>

using namespace std;

ApiHandler::ApiHandler(const string& url) : listener(url) {
    listener.support(methods::GET, [this](http_request req) { handle_get(req); });
    listener.support(methods::POST, [this](http_request req) { handle_post(req); });
    listener.support(methods::OPTIONS, [this](http_request req) { handle_options(req); });
}

ApiHandler::~ApiHandler() {
    stop();
}

void ApiHandler::handle_options(http_request request) {
    http_response response(status_codes::OK);
    
    response.headers().add("Access-Control-Allow-Origin", "*");
    response.headers().add("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response.headers().add("Access-Control-Allow-Headers", "Content-Type");
    
    request.reply(response);
}

void ApiHandler::handle_get(http_request request) {
    auto path = request.relative_uri().path();
    
    if(path == "/api/health") {
        handle_health(request);
    }
    else if(path == "/api/search") {
        handle_search(request);
    }
    else if(path == "/api/batch") {
        handle_batch(request);
    }
    else if(path == "/api/complexity") {
        handle_complexity(request);
    }
    else if(path == "/api/analyze") {
        handle_analyze(request);
    }
    else if(path == "/api/results") {
        handle_results(request);
    }
    else {
        request.reply(status_codes::NotFound, "Endpoint not found");
    }
}

void ApiHandler::handle_post(http_request request) {
    request.extract_json()
        .then([=](json::value request_body) {
            try {
                auto path = request.relative_uri().path();
                
                if(path == "/api/test") {
                    int size = request_body["size"].as_integer();
                    string algorithm = request_body["algorithm"].as_string();
                    
                    auto result = LinearSearchEngine::runBenchmark(size, algorithm);
                    
                    json::value response;
                    response["success"] = json::value::boolean(true);
                    response["data"] = json::value::object({
                        {"algorithm", json::value::string(result.algorithm)},
                        {"size", json::value::number(result.data_size)},
                        {"time_ns", json::value::number(result.execution_time_ns)},
                        {"comparisons", json::value::number(result.comparisons)},
                        {"found", json::value::boolean(result.found)}
                    });
                    
                    http_response resp(status_codes::OK);
                    resp.headers().add("Access-Control-Allow-Origin", "*");
                    resp.set_body(response);
                    request.reply(resp);
                }
            }
            catch(const exception& e) {
                json::value error;
                error["success"] = json::value::boolean(false);
                error["error"] = json::value::string(e.what());
                
                http_response resp(status_codes::BadRequest);
                resp.headers().add("Access-Control-Allow-Origin", "*");
                resp.set_body(error);
                request.reply(resp);
            }
        })
        .wait();
}

void ApiHandler::handle_health(http_request request) {
    json::value response;
    response["status"] = json::value::string("healthy");
    response["service"] = json::value::string("Linear Search API");
    response["version"] = json::value::string("1.0.0");
    
    http_response resp(status_codes::OK);
    resp.headers().add("Access-Control-Allow-Origin", "*");
    resp.set_body(response);
    request.reply(resp);
}

void ApiHandler::handle_search(http_request request) {
    auto query = uri::split_query(request.relative_uri().query());
    
    int size = 1000;
    string algorithm = "iterative";
    
    if(query.find("size") != query.end()) {
        size = stoi(query["size"]);
    }
    if(query.find("algorithm") != query.end()) {
        algorithm = query["algorithm"];
    }
    
    if(size <= 0 || size > 1000000) {
        json::value error;
        error["error"] = json::value::string("Size must be between 1 and 1,000,000");
        
        http_response resp(status_codes::BadRequest);
        resp.headers().add("Access-Control-Allow-Origin", "*");
        resp.set_body(error);
        request.reply(resp);
        return;
    }
    
    set<string> valid_algorithms = {"iterative", "recursive", "both"};
    if(valid_algorithms.find(algorithm) == valid_algorithms.end()) {
        algorithm = "iterative";
    }
    
    auto result = LinearSearchEngine::runBenchmark(size, algorithm);
    
    json::value response;
    response["success"] = json::value::boolean(true);
    response["data_size"] = json::value::number(size);
    response["algorithm"] = json::value::string(algorithm);
    response["execution_time_ns"] = json::value::number(result.execution_time_ns);
    response["execution_time_ms"] = json::value::number(result.execution_time_ns / 1000000.0);
    response["comparisons"] = json::value::number(result.comparisons);
    response["found"] = json::value::boolean(result.found);
    
    http_response resp(status_codes::OK);
    resp.headers().add("Access-Control-Allow-Origin", "*");
    resp.set_body(response);
    request.reply(resp);
}

void ApiHandler::handle_batch(http_request request) {
    auto query = uri::split_query(request.relative_uri().query());
    
    string sizes_str = "10,100,500,1000,5000,10000";
    if(query.find("sizes") != query.end()) {
        sizes_str = query["sizes"];
    }
    
    vector<int> sizes;
    size_t start = 0, end = 0;
    while((end = sizes_str.find(',', start)) != string::npos) {
        sizes.push_back(stoi(sizes_str.substr(start, end - start)));
        start = end + 1;
    }
    sizes.push_back(stoi(sizes_str.substr(start)));
    
    if(sizes.size() > 20) {
        sizes.resize(20);
    }
    
    json::value results = json::value::array();
    
    for(size_t i = 0; i < sizes.size(); i++) {
        int size = sizes[i];
        
        auto iter_result = LinearSearchEngine::runBenchmark(size, "iterative");
        auto rec_result = LinearSearchEngine::runBenchmark(size, "recursive");
        
        json::value item;
        item["size"] = json::value::number(size);
        item["iterative"] = json::value::object({
            {"time_ns", json::value::number(iter_result.execution_time_ns)},
            {"comparisons", json::value::number(iter_result.comparisons)}
        });
        item["recursive"] = json::value::object({
            {"time_ns", json::value::number(rec_result.execution_time_ns)},
            {"comparisons", json::value::number(rec_result.comparisons)}
        });
        
        results[i] = item;
    }
    
    json::value response;
    response["success"] = json::value::boolean(true);
    response["sizes_tested"] = json::value::number(sizes.size());
    response["results"] = results;
    
    http_response resp(status_codes::OK);
    resp.headers().add("Access-Control-Allow-Origin", "*");
    resp.set_body(response);
    request.reply(resp);
}

void ApiHandler::handle_complexity(http_request request) {
    json::value analysis;
    
    analysis["algorithm"] = json::value::string("Linear Search");
    
    analysis["time_complexity"] = json::value::object({
        {"best_case", json::value::string("O(1)")},
        {"average_case", json::value::string("O(n)")},
        {"worst_case", json::value::string("O(n)")}
    });
    
    analysis["space_complexity"] = json::value::object({
        {"iterative", json::value::string("O(1)")},
        {"recursive", json::value::string("O(n)")}
    });
    
    http_response resp(status_codes::OK);
    resp.headers().add("Access-Control-Allow-Origin", "*");
    resp.set_body(analysis);
    request.reply(resp);
}

void ApiHandler::handle_analyze(http_request request) {
    auto results = LinearSearchEngine::runPerformanceAnalysis();
    
    json::value response = json::value::array();
    
    for(size_t i = 0; i < results.size(); i++) {
        json::value item;
        item["size"] = json::value::number(results[i].size);
        item["iterative_time_ns"] = json::value::number(results[i].iterative_time_ns);
        item["recursive_time_ns"] = json::value::number(results[i].recursive_time_ns);
        item["iterative_comparisons"] = json::value::number(results[i].iterative_comparisons);
        item["recursive_comparisons"] = json::value::number(results[i].recursive_comparisons);
        
        response[i] = item;
    }
    
    http_response resp(status_codes::OK);
    resp.headers().add("Access-Control-Allow-Origin", "*");
    resp.set_body(response);
    request.reply(resp);
}

void ApiHandler::handle_results(http_request request) {
    ifstream file("performance_results.csv");
    if(!file.is_open()) {
        json::value error;
        error["error"] = json::value::string("No results available. Run /api/analyze first.");
        
        http_response resp(status_codes::NotFound);
        resp.headers().add("Access-Control-Allow-Origin", "*");
        resp.set_body(error);
        request.reply(resp);
        return;
    }
    
    stringstream buffer;
    buffer << file.rdbuf();
    
    http_response resp(status_codes::OK);
    resp.headers().add("Access-Control-Allow-Origin", "*");
    resp.headers().add("Content-Type", "text/csv");
    resp.set_body(buffer.str());
    request.reply(resp);
    
    file.close();
}

void ApiHandler::start() {
    try {
        listener.open().wait();
        cout << "Server started at: " << listener.uri().to_string() << endl;
        cout << "Available endpoints:" << endl;
        cout << "  GET  /api/health" << endl;
        cout << "  GET  /api/search?size=1000&algorithm=iterative" << endl;
        cout << "  GET  /api/batch?sizes=10,100,1000" << endl;
        cout << "  GET  /api/complexity" << endl;
        cout << "  GET  /api/analyze" << endl;
        cout << "  GET  /api/results" << endl;
        cout << "  POST /api/test" << endl;
    }
    catch(const exception& e) {
        cerr << "Error starting server: " << e.what() << endl;
    }
}

void ApiHandler::stop() {
    listener.close().wait();
    cout << "Server stopped" << endl;
}
