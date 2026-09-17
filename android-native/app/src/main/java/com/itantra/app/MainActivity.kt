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
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Call
import androidx.compose.material.icons.rounded.MailOutline
import androidx.compose.material.icons.rounded.Share
import androidx.compose.material3.Icon
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
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

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            val appViewModel: AppViewModel = viewModel()
            val isDarkTheme by appViewModel.isDarkTheme.collectAsState()

            ITantraTheme(isDarkTheme = isDarkTheme) {
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
        if (granted) {
            appViewModel.transmitter.startPtt(context)
        } else {
            appViewModel.transmitter.reportMicPermissionDenied()
        }
    }

    val throttleOn by appViewModel.throttle.enabled.collectAsState()
    val throttleBps by appViewModel.throttle.bitsPerSecond.collectAsState()
    val simNote = if (throttleOn) "SIM LINK · $throttleBps BPS" else null
    val pairingError by appViewModel.pairingError.collectAsState()

    val isDarkTheme by appViewModel.isDarkTheme.collectAsState()
    val onThemeToggle = { dark: Boolean -> appViewModel.setDarkTheme(dark) }

    Scaffold(
        containerColor = AppColor.Void,
        bottomBar = {
            BottomNavBar(
                mode = mode,
                onModeChange = { mode = it },
            )
        }
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
        ) {
            when (mode) {
                Mode.TRANSMIT -> TransmitterScreen(
                    transmitter = appViewModel.transmitter,
                    hasMicPermission = audioPermissionGranted,
                    onRequestMicPermission = { requestAudioPermission.launch(Manifest.permission.RECORD_AUDIO) },
                    linkNote = if (pairingError != null) "PAIRING FAILED · VERSION MISMATCH" else "TRANSMIT · OFFLINE",
                    simNote = simNote,
                    isDarkTheme = isDarkTheme,
                    onThemeToggle = onThemeToggle,
                )
                Mode.RECEIVE -> ReceiverScreen(
                    receiver = appViewModel.receiver,
                    linkNote = if (pairingError != null) "PAIRING FAILED · VERSION MISMATCH" else "RECEIVE · OFFLINE",
                    simNote = simNote,
                    isDarkTheme = isDarkTheme,
                    onThemeToggle = onThemeToggle,
                )
                Mode.LINK -> LinkScreen(
                    link = appViewModel.link,
                    throttle = appViewModel.throttle,
                    isDarkTheme = isDarkTheme,
                    onThemeToggle = onThemeToggle,
                )
            }
        }
    }
}

@Composable
private fun BottomNavBar(mode: Mode, onModeChange: (Mode) -> Unit) {
    Column {
        // Top hairline divider
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(1.dp)
                .background(AppColor.Hairline)
        )
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(AppColor.Surface)
                .padding(WindowInsets.navigationBars.asPaddingValues()),
        ) {
            NavTab(
                icon = Icons.Rounded.Call,
                label = "Transmit",
                active = mode == Mode.TRANSMIT,
                modifier = Modifier.weight(1f),
                onClick = { onModeChange(Mode.TRANSMIT) },
            )
            NavTab(
                icon = Icons.Rounded.MailOutline,
                label = "Receive",
                active = mode == Mode.RECEIVE,
                modifier = Modifier.weight(1f),
                onClick = { onModeChange(Mode.RECEIVE) },
            )
            NavTab(
                icon = Icons.Rounded.Share,
                label = "Link",
                active = mode == Mode.LINK,
                modifier = Modifier.weight(1f),
                onClick = { onModeChange(Mode.LINK) },
            )
        }
    }
}

@Composable
private fun NavTab(
    icon: ImageVector,
    label: String,
    active: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val iconTint = if (active) AppColor.Primary else AppColor.TextFaint
    val labelColor = if (active) AppColor.Primary else AppColor.TextFaint
    val bgColor = if (active) AppColor.Primary.copy(alpha = 0.10f) else androidx.compose.ui.graphics.Color.Transparent

    Box(
        modifier = modifier
            .background(bgColor, RoundedCornerShape(AppRadius.sm))
            .clickable(onClick = onClick)
            .padding(vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(
                imageVector = icon,
                contentDescription = label,
                tint = iconTint,
                modifier = Modifier.size(22.dp),
            )
            Text(
                text = label,
                color = labelColor,
                fontSize = 11.sp,
                fontWeight = if (active) FontWeight.Bold else FontWeight.Normal,
                modifier = Modifier.padding(top = 3.dp),
            )
        }
    }
}
