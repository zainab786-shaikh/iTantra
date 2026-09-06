package com.itantra.app

import android.Manifest
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.itantra.app.audio.AudioCapture
import com.itantra.app.diagnostics.AudioCaptureProbeSection
import com.itantra.app.diagnostics.PacketProbeSection
import com.itantra.app.diagnostics.SttPipelineProbeSection
import com.itantra.app.diagnostics.SttProbeSection
import com.itantra.app.diagnostics.TransportProbeSection
import com.itantra.app.diagnostics.TtsProbeSection
import com.itantra.app.diagnostics.VadSegmenterProbeSection
import com.itantra.app.diagnostics.ViewModelProbeSection
import com.itantra.app.ui.theme.AppColor
import com.itantra.app.ui.theme.AppSizing
import com.itantra.app.ui.theme.ITantraTheme

/**
 * Phase 1 shell only. This does not render the actual Transmit/Receive
 * product UI — that is Phase 10 (Compose UI) in MIGRATION_STATUS.md. Its
 * only job here is to prove the native Kotlin/Compose application launches,
 * renders with the ported design tokens, and carries no React Native
 * dependency, per Phase 1's exit criteria.
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        var audioPermissionGranted by mutableStateOf(AudioCapture.hasPermission(this))
        val requestAudioPermission = registerForActivityResult(
            ActivityResultContracts.RequestPermission()
        ) { granted -> audioPermissionGranted = granted }

        setContent {
            ITantraTheme {
                ShellScreen(
                    audioPermissionGranted = audioPermissionGranted,
                    onRequestAudioPermission = {
                        requestAudioPermission.launch(Manifest.permission.RECORD_AUDIO)
                    },
                )
            }
        }
    }
}

@Composable
private fun ShellScreen(
    audioPermissionGranted: Boolean,
    onRequestAudioPermission: () -> Unit,
) {
    Scaffold(
        containerColor = AppColor.Void,
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(AppColor.Void)
                .padding(innerPadding)
                .padding(AppSizing.screenPadding)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = "iTantra",
                color = AppColor.Text,
                fontSize = MaterialTheme.typography.titleLarge.fontSize,
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = "NATIVE SHELL · PHASE 1",
                color = AppColor.TextFaint,
                fontSize = MaterialTheme.typography.labelSmall.fontSize,
                fontWeight = FontWeight.Bold,
            )
            SttProbeSection()
            AudioCaptureProbeSection(
                hasPermission = audioPermissionGranted,
                onRequestPermission = onRequestAudioPermission,
            )
            VadSegmenterProbeSection(
                hasPermission = audioPermissionGranted,
                onRequestPermission = onRequestAudioPermission,
            )
            SttPipelineProbeSection(
                hasPermission = audioPermissionGranted,
                onRequestPermission = onRequestAudioPermission,
            )
            PacketProbeSection()
            TransportProbeSection()
            TtsProbeSection()
            ViewModelProbeSection(
                hasPermission = audioPermissionGranted,
                onRequestPermission = onRequestAudioPermission,
            )
        }
    }
}

@Preview
@Composable
private fun ShellScreenPreview() {
    ITantraTheme {
        ShellScreen(audioPermissionGranted = false, onRequestAudioPermission = {})
    }
}
