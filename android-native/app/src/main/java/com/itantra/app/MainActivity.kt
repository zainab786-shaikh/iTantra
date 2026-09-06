package com.itantra.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
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
        setContent {
            ITantraTheme {
                ShellScreen()
            }
        }
    }
}

@Composable
private fun ShellScreen() {
    Scaffold(
        containerColor = AppColor.Void,
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(AppColor.Void)
                .padding(innerPadding)
                .padding(AppSizing.screenPadding),
            verticalArrangement = Arrangement.Center,
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
        }
    }
}

@Preview
@Composable
private fun ShellScreenPreview() {
    ITantraTheme {
        ShellScreen()
    }
}
