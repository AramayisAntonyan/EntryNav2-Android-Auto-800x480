// Copyright (c) 2026 Jiraiya43
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// The ids of hmiapp/AuxCamera.xml the Camera app's code uses (tests/test_rhmi_ids.py checks the XML's tables).
#ifndef ENTRY_AAD_HMIAPP_CAMERA_IDS_HPP
#define ENTRY_AAD_HMIAPP_CAMERA_IDS_HPP

namespace evo {
namespace app {
namespace camera {

const unsigned kPreviewState = 700;  // the entry's preview card
const unsigned kCameraState = 710;   // the picture
const unsigned kActEntry = 19;       // the entry's combinedAction 11: raAction 19, hmiAction 20 -> 710
const unsigned kActSize0 = 30;       // OPTION: 30 480x320, 31 640x480, 32 720x480
const unsigned kActRestart = 33;     // OPTION: restart the capture
const unsigned kSizeCount = 3;
const unsigned kFrameModel0 = 40;   // raImageModel per size (one model per component: shared, a picture went astray)
const unsigned kFrameImage0 = 100;  // image component per size, hidden but the chosen one while pictures come
const unsigned kMessageModel = 50;  // over the picture: no adapter, no signal (empty while pictures come)
const unsigned kStatusModel = 51;   // under it: size, pictures per second, sent / not read
const unsigned kImgIcon = 54001;    // the entry's icon (AuxCamera_ALL_Images.zip)
const unsigned kImgCard = 54002;    // the preview card
const unsigned kHandlerVisible = 11;
const unsigned kPropVisible = 3;

}  // namespace camera
}  // namespace app
}  // namespace evo

#endif
