/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/errors.h"

//cfusa:req REQ-WIREERR-002
const char *rcp_wire_error_string(rcp_wire_error_t e)
{
    switch (e) {
    case RCP_ERROR_NONE:                      return "wire: no error";
    case RCP_ERROR_UNSUPPORTED_CMD:           return "wire: unsupported command";
    case RCP_ERROR_SEQUENCER_NOT_KNOWN:       return "wire: sequencer not known";
    case RCP_ERROR_UNAUTHORIZED_ACCESS:       return "wire: unauthorized access";
    case RCP_ERROR_LOCKED_MEM_ACCESS:         return "wire: locked memory access";
    case RCP_ERROR_REQUEST_CANCELED:          return "wire: request canceled";
    case RCP_ERROR_REQUEST_NOT_FOUND:         return "wire: request not found";
    case RCP_ERROR_EP_ERROR:                  return "wire: endpoint error";
    case RCP_ERROR_EP_NOT_FOUND:              return "wire: endpoint not found";
    case RCP_ERROR_PWM_IN_NO_SIGNAL:          return "wire: PWM input has no signal";
    case RCP_ERROR_REQUEST_STORAGE_OVERFLOW:  return "wire: request storage overflow";
    case RCP_ERROR_REQUEST_REJECTED:          return "wire: request rejected";
    case RCP_ERROR_POCI_FAILURE:              return "wire: POCI failure (CRC32 mismatch)";
    case RCP_ERROR_PRESENTATION_TIME_TOO_FAR: return "wire: presentation time too far";
    case RCP_ERROR_GPTP_FAIL:                 return "wire: gPTP failure";
    case RCP_ERROR_INVALID_PARAMETER:         return "wire: invalid parameter";
    case RCP_ERROR_CHAIN_ABORTED:             return "wire: chain aborted";
    case RCP_ERROR_CHAIN_ERROR:               return "wire: chain error";
    default:                                  return "wire: unknown error code";
    }
}
