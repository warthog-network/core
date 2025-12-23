#pragma once

#include "general/errors_forward.hpp"
#include <cstdint>
////////////////////////////////////
// LIST OF ERROR CODES            //
////////////////////////////////////
// These codes are used to describe why a connection was
// closed by a peer or by our end.

// LIBUV ERROR CODES
// -----------------
// Libuv error codes  are negative and are returned
// by libuv routines are mainly network related such as
// host unreachable, address already in use, broken pipe
// etc.

// ADDITIONAL ERROR CODES
// -------------------
// Misbehaviour, range [1-999]:
// Other codes, range [1000-1999]
#define ADDITIONAL_ERRNO_MAP(XX)                                                   \
    /*001 - 200: Errors*/                                                          \
    XX(0, ENOERROR, "no error")                                                    \
    XX(1, EMSGTYPE, "invalid message type")                                        \
    XX(2, EMSGLEN, "invalid message length")                                       \
    XX(4, ECHECKSUM, "bad message checksum")                                       \
    XX(5, EMSGFLOOD, "received too many messages")                                 \
    XX(6, ENOBATCH, "peer did not provide batch")                                  \
    XX(7, EBUFFERFULL, "send buffer full")                                         \
    XX(8, EBATCHSIZE, "invalid batch size")                                        \
    XX(9, EHEADERLINK, "bad header link")                                          \
    XX(10, EPOW, "bad proof of work")                                              \
    XX(11, ETIMESTAMP, "timestamp rule violated")                                  \
    XX(12, EDIFFICULTY, "wrong difficulty in block header")                        \
    XX(13, EHANDSHAKE, "bad hand shake")                                           \
    XX(14, EVERSION, "unsupported version")                                        \
    XX(15, EREORGWORK, "peer changed to shorter chain")                            \
    XX(16, EDESCRIPTOR, "descriptors not consecutive")                             \
    XX(17, EMROOT, "merkle root mismatch")                                         \
    XX(18, ENOBLOCK, "peer did not provide block")                                 \
    XX(19, EUNREQUESTED, "received unrequested message")                           \
    XX(20, EIDNOTREFERENCED, "account id not referenced")                          \
    XX(21, EADDRPOLICY, "new address policy violated")                             \
    XX(22, EBALANCE, "insufficient balance")                                       \
    XX(23, ECORRUPTEDSIG, "corrupted signature")                                   \
    XX(24, ETIMEOUT, "connection request timed out")                               \
    XX(25, ESWITCHING, "busy, switching chains")                                   \
    XX(26, ENONCE, "duplicate transaction nonce")                                  \
    XX(27, EDUST, "fee too low")                                                   \
    XX(28, EBLOCKSIZE, "block too large")                                          \
    XX(29, EPINHEIGHT, "invalid transaction pin")                                  \
    XX(30, ECLOCKTOLERANCE, "clock tolerance exceeded")                            \
    XX(31, EINVDSC, "invalid descripted state")                                    \
    XX(32, EAPPEND, "invalid chain append")                                        \
    XX(33, EFORK, "invalid chain fork")                                            \
    XX(56, ENOTFOUND, "not found")                                                 \
    XX(57, EEMPTY, "empty response for request not yet expired")                   \
    XX(58, EFAKEHEIGHT, "fake height advertised by node")                          \
    XX(59, EFAKEWORK, "fake total work advertised by node")                        \
    XX(60, EBADMATCH, "bad headerchain match")                                     \
    XX(61, EBADMISMATCH, "bad headerchain mismatch")                               \
    XX(62, EBADPROBE, "inconsistent probe message")                                \
    XX(63, EPROBEDESCRIPTOR, "current probe descriptor does not match")            \
    XX(64, ERESTRICTED, "peer ignored limit restrictions")                         \
    XX(65, ENOPINHEIGHT, "height is no pin height")                                \
    XX(66, EBADLEADER, "bad leader signature")                                     \
    XX(67, ELEADERMISMATCH, "leader signature mismatch")                           \
    XX(68, ELOWPRIORITY, "low leader signature priority")                          \
    XX(69, EBADPUBKEY, "invalid public key")                                       \
    XX(70, EBADPRIVKEY, "invalid private key")                                     \
    XX(71, EBADADDRESS, "invalid address")                                         \
    XX(72, EBADHEIGHT, "invalid height")                                           \
    XX(73, EZEROHEIGHT, "invalid zero height")                                     \
    XX(74, EBADROLLBACK, "rollback forbidden")                                     \
    XX(75, EBADROLLBACKLEN, "bad rollback length")                                 \
    XX(76, EMINEDDEPRECATED, "submitted deprecated block")                         \
    XX(77, EBLOCKRANGE, "invalid block range")                                     \
    XX(78, EFORKHEIGHT, "invalid fork height")                                     \
    XX(79, EPROBEHEIGHT, "invalid probe height")                                   \
    XX(80, EBATCHHEIGHT, "invalid batch height")                                   \
    XX(81, EGRIDMISMATCH, "grid mismatch")                                         \
    XX(82, ESELFSEND, "self send transaction not allowed")                         \
    XX(83, EBLOCKVERSION, "unsupported block version")                             \
    XX(84, EZEROAMOUNT, "transactions cannot send 0 tokens")                       \
    XX(85, ENOINIT, "first message must be init message")                          \
    XX(86, EINVINIT, "only first message can be init message")                     \
    XX(87, EFAKEACCID, "fake account id")                                          \
    XX(88, EINV_FUNDS, "malformed funds data")                                     \
    XX(89, EINV_BODY, "malformed body data")                                       \
    XX(90, EINV_PAGE, "invalid page")                                              \
    XX(91, EINV_PROBE, "invalid probe message")                                    \
    XX(92, EINV_GRID, "invalid grid")                                              \
    XX(93, EINV_TXREQ, "invalid tx request")                                       \
    XX(94, EINV_ARGS, "invalid API arguments")                                     \
    XX(95, EINV_TXREP, "invalid tx reply")                                         \
    XX(96, EINV_INITGRID, "invalid grid in init message")                          \
    XX(97, EINV_HEADERVEC, "invalid header vector")                                \
    XX(98, EINV_BLOCKREPSIZE, "invalid block reply size")                          \
    XX(99, EINV_RTCOFFER, "Invalid SDP offer forward request")                     \
    XX(100, EDUP_RTCOFFER, "Duplicate SDP offer forward request")                  \
    XX(101, ERTCINV_RFA, "Invalid RTC request forward answer")                     \
    XX(102, ERTCDUP_RFA, "Duplicate RTC request forward answer")                   \
    XX(103, ERTCINV_FA, "Invalid RTC forwarded answer")                            \
    XX(104, ERTCDUP_FA, "Duplicate RTC forwarded answer")                          \
    XX(105, ERTCIP_FA, "WebRTC forwarded ip differs")                              \
    XX(106, ERTCDISCARD_FA, "Invalid discarding of expected forwarded answer")     \
    XX(107, ERTCQUOTA_FO, "RTC quota for forwarded offers exceeded")               \
    XX(109, ERTCDUP_ID, "Duplicate RTC id")                                        \
    XX(110, ERTCFAILED, "WebRTC connection failed")                                \
    XX(111, ERTCCHANNEL_ERROR, "WebRTC channel error")                             \
    XX(112, ERTCCLOSED, "WebRTC connection was closed")                            \
    XX(113, ERTCTEXT, "Text over RTC is not supported")                            \
    XX(114, ERTCUNIQUEIP, "WebRTC SDP ip is not unique")                           \
    XX(115, ERTCUNIQUEIP_RFA, "WebRTC SDP ip not unique (forward answer)")         \
    XX(116, ERTCWRONGIP_RFA, "Wrong WebRTC SDP ip (forward answer)")               \
    XX(117, ERTCWRONGIP_FO, "Wrong WebRTC SDP ip (forward offer)")                 \
    XX(118, ERTCUNIQUEIP_RFO, "WebRTC forward offer request SDP ip is not unique") \
    XX(119, ERTCUNVERIFIEDIP, "WebRTC ip not verified")                            \
    XX(120, ERTCDUP_DATACHANNEL, "WebRTC ip not verified")                         \
    XX(121, ERTCUNEXP_VA, "Unexpected WebRTC verification answer")                 \
    XX(122, EADDRNOTFOUND, "address not found")                                    \
    XX(123, EACCIDNOTFOUND, "account id not found")                                \
    XX(124, ECONNRATELIMIT, "connection rate limit exceeded")                      \
    XX(125, EINITV1, "Init V1 not allowed from this peer")                         \
    XX(126, EINITV3, "Init V3 not allowed from this peer")                         \
    XX(127, EFROZENACC, "account is frozen and can't send")                        \
    XX(128, EHEADERRANGE, "invalid header range")                                  \
    XX(129, EBADPRICE, "invalid price")                                            \
    XX(130, EBADFEE, "invalid fee")                                                \
    XX(131, ETOKIDNOTFOUND, "token id not found")                                  \
    XX(132, ENOPOOL, "no pool liquidity")                                          \
    XX(133, ERTCDISABLED, "WebRTC disabled, cannot receive message")               \
    XX(134, EIDPOLICY, "block transaction id policy violated")                     \
    XX(181, ETXTYPESTATE, "Transaction type invalud at current chain state")       \
    XX(182, EASSETNAME, "invalid asset name")                                      \
    XX(183, EZEROBASEQUOTE, "Base and quote can't be both zero")                   \
    XX(184, ETOKBALANCE, "insufficient token balance")                             \
    XX(185, EASSETHASHNOTFOUND, "asset hash not found")                            \
    XX(186, EASSETIDNOTFOUND, "asset id not found")                                \
    XX(187, ECANCELSELF, "Cannot cancel future transactions")                      \
    XX(188, ECANCELFUTURE, "Cannot cancel future transactions")                    \
    XX(189, ETXTYPE, "Invalid transaction message type")                           \
    XX(190, EEXCESSBYTES, "Excessive bytes after parsing")                         \
    XX(191, EWARTTOKID, "Illegal use of WART in non-WART token id")                \
    XX(192, EBLOCKV4, "Cannot use BlockV4 features")                               \
    XX(193, EZEROWART, "transactions cannot send 0 WART")                          \
    XX(194, EPOOLREDEEM, "Cannot redeem more shares than pool issued")             \
    XX(195, EINVBUY, "invalid buy value")                                          \
    XX(196, ETOKENPRECISION, "invalid token precision")                            \
    XX(197, EINVSIG, "invalid signature")                                          \
    XX(198, EBATCHSIZE2, "invalid batch size")                                     \
    XX(199, EBATCHSIZE3, "invalid batch size")                                     \
    /*200 - 299: Errors not leading to ban*/                                       \
    XX(200, ERTCNOSIGNAL, "WebRTC signaling server was closed (offer)")            \
    XX(201, ERTCNOSIGNAL2, "WebRTC signaling server was closed (answer)")          \
    XX(202, EMSGINTEGRITY, "message integrity check failed")                       \
    XX(203, ESTARTWEBSOCK, "Error while starting websocket connection")            \
    XX(204, EWEBSOCK, "Error while in websocket connection")                       \
    XX(205, ERTCOWNIP, "Own announced IP not present in ")                         \
    XX(206, ERTCFWDREJECT, "WebRTC forward rejected")                              \
    XX(207, ERTCCHANNEL_CLOSED, "WebRTC datachannel closed")                       \
    XX(208, ERTCNOPEER, "WebRTC verification peer already closed")                 \
    XX(209, ERTCNOIP, "Cannot select own WebRTC ip")                               \
    XX(210, ERTCIDIP, "IP in verification offer is not announced as identity")     \
    XX(211, ERTCFEELER, "Normal feeler connection shutdown")                       \
    XX(213, EAPICMD, "Triggered by API command")                                   \
    XX(214, EMINFEE, "transaction fee below threshold")                            \
    /*300 - 399: API triggered errors*/                                            \
    XX(300, EINV_HEX, "cannot parse hexadecimal input")                            \
    XX(301, EBADNONCE, "cannot parse nonce")                                       \
    XX(302, EINEXACTFEE, "inexact fee not allowed")                                \
    XX(303, EBADAMOUNT, "invalid amount")                                          \
    XX(304, EPARSESIG, "cannot parse signature")                                   \
    XX(305, ENOTSYNCED, "node not synced yet")                                     \
    XX(306, EBADTOKEN, "invalid token")                                            \
    XX(307, EINV_TOKEN, "malformed token specification")                           \
    XX(308, EPARSEHASH, "cannot parse hash")                                       \
    XX(309, EBADLIQUIDITYFLAG, "cannot parse liquidity flag")                      \
    XX(310, EBADBUYFLAG, "cannot parse buy flag")                                  \
    XX(311, EBADCANCELHEIGHT, "cannot parse cancel height")                        \
    XX(312, EBADCANCELNONCE, "cannot parse cancel nonce")                          \
    XX(313, EBADASSETUNITS, "cannot parse asset units")                            \
    XX(314, EBADASSETPRECISION, "cannot parse asset precision")                    \
    XX(315, BADHEADER, "cannot parse header")                                      \
    XX(1000, ESIGTERM, "received SIGTERM")                                         \
    XX(1001, ESIGHUP, "received SIGHUP")                                           \
    XX(1002, ESIGINT, "received SIGINT")                                           \
    XX(1003, EREFUSED, "connection refused due to ban")                            \
    XX(1005, EDUPLICATECONNECTION, "duplicate connection")                         \
    XX(1006, EEVICTED, "connection was evicted")                                   \
    XX(2000, EBUG, "bug-related error")

#define ERR_DEFINE(code, name, _) constexpr int32_t name = code;
ADDITIONAL_ERRNO_MAP(ERR_DEFINE)
#undef ERR_DEFINE
