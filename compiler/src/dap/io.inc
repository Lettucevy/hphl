// ---------------------------------------------------------------------------
// IO DAP
// ---------------------------------------------------------------------------
JsonValue DapServer::makeResponse(const JsonValue& id) {
  JsonValue r = JsonValue::makeObject();
  r["jsonrpc"] = JsonValue::makeString("2.0");
  r["id"] = id;
  return r;
}

void DapServer::respond(const JsonValue& id, const JsonValue& result) {
  JsonValue r = makeResponse(id);
  r["result"] = result;
  writeFrame(r.serialize());
}

void DapServer::respondError(const JsonValue& id, long long code,
                             const std::string& msg) {
  JsonValue r = makeResponse(id);
  JsonValue e = JsonValue::makeObject();
  e["code"] = JsonValue::makeInt(code);
  e["message"] = JsonValue::makeString(msg);
  r["error"] = e;
  writeFrame(r.serialize());
}

void DapServer::sendStopped(const std::string& reason, const std::string& desc) {
  JsonValue p = JsonValue::makeObject();
  p["reason"] = JsonValue::makeString(reason);
  if (!desc.empty()) p["description"] = JsonValue::makeString(desc);
  p["threadId"] = JsonValue::makeInt(pausedTid_);
  p["allThreadsStopped"] = JsonValue::makeBool(true);
  sendEvent("stopped", p);
}

// ---------------------------------------------------------------------------
