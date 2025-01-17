// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_HEARTBEAT_SENDER_H_
#define REMOTING_HOST_HEARTBEAT_SENDER_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "net/base/backoff_entry.h"
#include "remoting/base/grpc_support/grpc_channel.h"
#include "remoting/base/rsa_key_pair.h"
#include "remoting/proto/remoting/v1/directory_messages.pb.h"
#include "remoting/signaling/muxing_signal_strategy.h"

namespace base {
class TimeDelta;
}  // namespace base

namespace grpc {
class Status;
}  // namespace grpc

namespace remoting {

class LogToServer;
class OAuthTokenGetter;
class RsaKeyPair;

// HeartbeatSender periodically sends heartbeat to the directory service. See
// the HeartbeatRequest message in directory_messages.proto for more details.
//
// Normally the heartbeat indicates that the host is healthy and ready to
// accept new connections from a client, but the message can optionally include
// a host_offline_reason field, which indicates that the host cannot accept
// connections from the client (and might possibly be shutting down).  The value
// of the host_offline_reason field can be either a string from
// host_exit_codes.cc (i.e. "INVALID_HOST_CONFIGURATION" string) or one of
// kHostOfflineReasonXxx constants (i.e. "POLICY_READ_ERROR" string).
//
// The sequence_id field of the heartbeat is a zero-based incrementally
// increasing integer unique to each heartbeat from a single host.
// The server checks the value, and if it is incorrect, includes the
// correct value in the result stanza. The host should then send another
// heartbeat, with the correct sequence-id, and increment the sequence-id in
// susbequent heartbeats.
// The signature is a base-64 encoded SHA-1 hash, signed with the host's
// private RSA key. The message being signed is the full Jid concatenated with
// the sequence-id, separated by one space. For example, for the heartbeat
// stanza above, the message that is signed is
// "user@gmail.com/chromoting_ftl_abc123 456".
//
// The sends a HeartbeatResponse in response to each successful heartbeat.
class HeartbeatSender final : public SignalStrategy::Listener {
 public:
  // Signal strategies and |oauth_token_getter| must outlive this object.
  // Heartbeats will start when either both of the signal strategies enter the
  // CONNECTED state, or one of the strategy has been in CONNECTED state for
  // a specific time interval.
  //
  // |on_heartbeat_successful_callback| is invoked after the first successful
  // heartbeat.
  //
  // |on_unknown_host_id_error| is invoked when the host ID is permanently not
  // recognized by the server.
  //
  // |on_auth_error| is invoked when the heartbeat sender permanently fails to
  // authenticate the requests.
  HeartbeatSender(base::OnceClosure on_heartbeat_successful_callback,
                  base::OnceClosure on_unknown_host_id_error,
                  base::OnceClosure on_auth_error,
                  const std::string& host_id,
                  MuxingSignalStrategy* signal_strategy,
                  const scoped_refptr<const RsaKeyPair>& host_key_pair,
                  OAuthTokenGetter* oauth_token_getter,
                  LogToServer* log_to_server);
  ~HeartbeatSender() override;

  // Sets host offline reason for future heartbeat, and initiates sending a
  // heartbeat right away.
  //
  // For discussion of allowed values for |host_offline_reason| argument,
  // please see the description in the class-level comments above.
  //
  // |ack_callback| will be called when the server acks receiving the
  // |host_offline_reason| or when |timeout| is reached.
  void SetHostOfflineReason(
      const std::string& host_offline_reason,
      const base::TimeDelta& timeout,
      base::OnceCallback<void(bool success)> ack_callback);

 private:
  class HeartbeatClient;

  friend class HeartbeatSenderTest;

  void SetGrpcChannelForTest(GrpcChannelSharedPtr channel);

  // SignalStrategy::Listener interface.
  void OnSignalStrategyStateChange(SignalStrategy::State state) override;
  bool OnSignalStrategyIncomingStanza(
      const jingle_xmpp::XmlElement* stanza) override;

  void SendHeartbeat();
  void OnResponse(const grpc::Status& status,
                  const apis::v1::HeartbeatResponse& response);

  // Handlers for host-offline-reason completion and timeout.
  void OnHostOfflineReasonTimeout();
  void OnHostOfflineReasonAck();

  void OnWaitForAllStrategiesConnectedTimeout();

  // Helper methods used by DoSendStanza() to generate heartbeat stanzas.
  apis::v1::HeartbeatRequest CreateHeartbeatRequest();
  std::string CreateSignature(const std::string& signaling_id);

  base::OnceClosure on_heartbeat_successful_callback_;
  base::OnceClosure on_unknown_host_id_error_;
  base::OnceClosure on_auth_error_;
  std::string host_id_;
  MuxingSignalStrategy* const signal_strategy_;
  scoped_refptr<const RsaKeyPair> host_key_pair_;
  std::unique_ptr<HeartbeatClient> client_;
  LogToServer* const log_to_server_;
  OAuthTokenGetter* const oauth_token_getter_;

  base::OneShotTimer heartbeat_timer_;

  net::BackoffEntry backoff_;

  int sequence_id_ = 0;
  bool heartbeat_succeeded_ = false;

  // Fields to send and indicate completion of sending host-offline-reason.
  std::string host_offline_reason_;
  base::OnceCallback<void(bool success)> host_offline_reason_ack_callback_;
  base::OneShotTimer host_offline_reason_timeout_timer_;

  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(HeartbeatSender);
};

}  // namespace remoting

#endif  // REMOTING_HOST_HEARTBEAT_SENDER_H_
