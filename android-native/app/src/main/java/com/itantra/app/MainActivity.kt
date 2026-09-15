package com.itantra.app

import android.Manifest
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.itantra.app.audio.AudioCapture
import com.itantra.app.ui.screens.LinkScreen
import com.itantra.app.ui.screens.ReceiverScreen
import com.itantra.app.ui.screens.TransmitterScreen
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppRadius
import com.itantra.app.ui.theme.ITantraTheme
import com.itantra.app.viewmodel.AppViewModel

private enum class Mode { TRANSMIT, RECEIVE, LINK }

/**
 * The actual iTantra product UI (Phase 10). Direct port of App.tsx's root
 * composition - one AppViewModel (owning the shared MockTransport + both
 * controllers, see AppViewModel.kt) and a floating Transmit/Receive
 * switcher, exactly mirroring App.tsx's own `mode` state and
 * `ModeSwitcher`. No Home/Devices/History/Settings/dashboards were added -
 * this is the same two-screen product, just natively implemented.
 *
 * The temporary diagnostic probe sections used during Phases 2-9 to
 * verify each ported component on-device have been removed (Phase 13
 * cleanup) now that the real product UI exercises the same code paths.
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            ITantraTheme {
                val appViewModel: AppViewModel = viewModel()
                AppShell(appViewModel)
            }
        }
    }
}

@Composable
private fun AppShell(appViewModel: AppViewModel) {
    val context = LocalContext.current
    var mode by remember { mutableStateOf(Mode.TRANSMIT) }
    var audioPermissionGranted by remember { mutableStateOf(AudioCapture.hasPermission(context)) }
    val requestAudioPermission = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        audioPermissionGranted = granted
        // Mirrors useTransmitterController.ts's startPtt(), which awaits
        // requestRecordingPermissionsAsync() and proceeds to open the mic in
        // the same call if granted (or reports the exact same denial message
        // if not). Android's permission request is an Activity-level
        // operation the ViewModel cannot itself await mid-press, so the UI
        // layer requests it and reports the outcome back here instead of
        // silently requiring a second press to actually start capturing.
        if (granted) {
            appViewModel.transmitter.startPtt(context)
        } else {
            appViewModel.transmitter.reportMicPermissionDenied()
        }
    }

    // Composed once, here, so both cockpit screens state the same thing.
    val throttleOn by appViewModel.throttle.enabled.collectAsState()
    val throttleBps by appViewModel.throttle.bitsPerSecond.collectAsState()
    // "SIM LINK", not "SIMULATED LINK": the longer label clipped at this
    // width, and dropping the BPS unit instead would have left a bare number
    // that says nothing. The Link screen carries the full wording.
    val simNote = if (throttleOn) "SIM LINK · $throttleBps BPS" else null
    // C-34: a refused pairing is shown in place of the link line on both screens.
    val pairingError by appViewModel.pairingError.collectAsState()

    Box(modifier = Modifier.fillMaxSize().background(AppColor.Void)) {
        when (mode) {
            Mode.TRANSMIT -> TransmitterScreen(
                transmitter = appViewModel.transmitter,
                hasMicPermission = audioPermissionGranted,
                onRequestMicPermission = { requestAudioPermission.launch(Manifest.permission.RECORD_AUDIO) },
                linkNote = if (pairingError != null) "PAIRING FAILED · VERSION MISMATCH" else "TRANSMIT · OFFLINE",
                simNote = simNote,
            )
            Mode.RECEIVE -> ReceiverScreen(
                receiver = appViewModel.receiver,
                linkNote = if (pairingError != null) "PAIRING FAILED · VERSION MISMATCH" else "RECEIVE · OFFLINE",
                simNote = simNote,
            )
            Mode.LINK -> LinkScreen(link = appViewModel.link, throttle = appViewModel.throttle)
        }

        Row(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(bottom = 12.dp)
                .padding(WindowInsets.navigationBars.asPaddingValues())
                .background(AppColor.Surface, RoundedCornerShape(AppRadius.pill))
                .padding(4.dp),
        ) {
            SwitchButton(label = "Transmit", active = mode == Mode.TRANSMIT, onClick = { mode = Mode.TRANSMIT })
            SwitchButton(label = "Receive", active = mode == Mode.RECEIVE, onClick = { mode = Mode.RECEIVE })
            SwitchButton(label = "Link", active = mode == Mode.LINK, onClick = { mode = Mode.LINK })
        }
    }
}

@Composable
private fun SwitchButton(label: String, active: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .background(
                if (active) AppColor.Primary.copy(alpha = 0.15f) else androidx.compose.ui.graphics.Color.Transparent,
                RoundedCornerShape(AppRadius.pill),
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 22.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = label,
            color = if (active) AppColor.Primary else AppColor.TextMuted,
            fontSize = 13.sp,
            fontWeight = FontWeight.Bold,
        )
    }
}
