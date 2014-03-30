/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <OMX_Core.h>
#include <glib.h>

/* stolen from gst-omx */
const gchar *
gst_omx_error_to_string (OMX_ERRORTYPE err)
{
  switch (err) {
    case OMX_ErrorNone:
      return "None";
    case OMX_ErrorInsufficientResources:
      return "Insufficient resources";
    case OMX_ErrorUndefined:
      return "Undefined";
    case OMX_ErrorInvalidComponentName:
      return "Invalid component name";
    case OMX_ErrorComponentNotFound:
      return "Component not found";
    case OMX_ErrorInvalidComponent:
      return "Invalid component";
    case OMX_ErrorBadParameter:
      return "Bad parameter";
    case OMX_ErrorNotImplemented:
      return "Not implemented";
    case OMX_ErrorUnderflow:
      return "Underflow";
    case OMX_ErrorOverflow:
      return "Overflow";
    case OMX_ErrorHardware:
      return "Hardware";
    case OMX_ErrorInvalidState:
      return "Invalid state";
    case OMX_ErrorStreamCorrupt:
      return "Stream corrupt";
    case OMX_ErrorPortsNotCompatible:
      return "Ports not compatible";
    case OMX_ErrorResourcesLost:
      return "Resources lost";
    case OMX_ErrorNoMore:
      return "No more";
    case OMX_ErrorVersionMismatch:
      return "Version mismatch";
    case OMX_ErrorNotReady:
      return "Not ready";
    case OMX_ErrorTimeout:
      return "Timeout";
    case OMX_ErrorSameState:
      return "Same state";
    case OMX_ErrorResourcesPreempted:
      return "Resources preempted";
    case OMX_ErrorPortUnresponsiveDuringAllocation:
      return "Port unresponsive during allocation";
    case OMX_ErrorPortUnresponsiveDuringDeallocation:
      return "Port unresponsive during deallocation";
    case OMX_ErrorPortUnresponsiveDuringStop:
      return "Port unresponsive during stop";
    case OMX_ErrorIncorrectStateTransition:
      return "Incorrect state transition";
    case OMX_ErrorIncorrectStateOperation:
      return "Incorrect state operation";
    case OMX_ErrorUnsupportedSetting:
      return "Unsupported setting";
    case OMX_ErrorUnsupportedIndex:
      return "Unsupported index";
    case OMX_ErrorBadPortIndex:
      return "Bad port index";
    case OMX_ErrorPortUnpopulated:
      return "Port unpopulated";
    case OMX_ErrorComponentSuspended:
      return "Component suspended";
    case OMX_ErrorDynamicResourcesUnavailable:
      return "Dynamic resources unavailable";
    case OMX_ErrorMbErrorsInFrame:
      return "Macroblock errors in frame";
    case OMX_ErrorFormatNotDetected:
      return "Format not detected";
    case OMX_ErrorContentPipeOpenFailed:
      return "Content pipe open failed";
    case OMX_ErrorContentPipeCreationFailed:
      return "Content pipe creation failed";
    case OMX_ErrorSeperateTablesUsed:
      return "Separate tables used";
    case OMX_ErrorTunnelingUnsupported:
      return "Tunneling unsupported";
    default:
      if (err >= (guint32) OMX_ErrorKhronosExtensions
          && err < (guint32) OMX_ErrorVendorStartUnused) {
        return "Khronos extension error";
      } else if (err >= (guint32) OMX_ErrorVendorStartUnused
          && err < (guint32) OMX_ErrorMax) {
        return "Vendor specific error";
      } else {
        return "Unknown error";
      }
  }
}

const gchar *
gst_omx_state_to_string (OMX_STATETYPE state)
{
  switch (state) {
    case OMX_StateInvalid:
      return "Invalid";
    case OMX_StateLoaded:
      return "Loaded";
    case OMX_StateIdle:
      return "Idle";
    case OMX_StateExecuting:
      return "Executing";
    case OMX_StatePause:
      return "Pause";
    case OMX_StateWaitForResources:
      return "WaitForResources";
    default:
      if (state >= OMX_StateKhronosExtensions)
        return "KhronosExtensionState";
      else if (state >= OMX_StateVendorStartUnused)
        return "CustomVendorState";
      break;
  }
  return "Unknown state";
}

const gchar *
gst_omx_command_to_string (OMX_COMMANDTYPE cmd)
{
  switch (cmd) {
    case OMX_CommandStateSet:
      return "SetState";
    case OMX_CommandFlush:
      return "Flush";
    case OMX_CommandPortDisable:
      return "DisablePort";
    case OMX_CommandPortEnable:
      return "EnablePort";
    case OMX_CommandMarkBuffer:
      return "MarkBuffer";
    default:
      if (cmd >= OMX_CommandKhronosExtensions)
        return "KhronosExtensionCommand";
      else if (cmd >= OMX_CommandVendorStartUnused)
        return "VendorExtensionCommand";
      break;
  }
  return "Unknown command";
}
