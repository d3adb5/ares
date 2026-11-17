<script setup>
import { ref } from 'vue'
import PlatformLayout from '../../PlatformLayout.vue'
import Checkbox from "../../Checkbox.vue";

const props = defineProps([
  'platform',
  'config'
])

const config = ref(props.config)
</script>

<template>
  <div class="config-page">
    <!-- FEC Percentage -->
    <div class="mb-3">
      <label for="fec_percentage" class="form-label">{{ $t('config.fec_percentage') }}</label>
      <input type="text" class="form-control" id="fec_percentage" placeholder="20" v-model="config.fec_percentage" />
      <div class="form-text">{{ $t('config.fec_percentage_desc') }}</div>
    </div>

    <!-- Ares: opt-in stream tuning -->
    <div class="mb-3 form-check">
      <input type="checkbox" class="form-check-input" id="fec_adaptive" v-model="config.fec_adaptive" true-value="enabled" false-value="disabled" />
      <label for="fec_adaptive" class="form-check-label">{{ $t('config.fec_adaptive') }}</label>
      <div class="form-text">{{ $t('config.fec_adaptive_desc') }}</div>
    </div>
    <div class="mb-3" v-if="platform === 'linux'">
      <label for="stream_thread_priority" class="form-label">{{ $t('config.stream_thread_priority') }}</label>
      <select id="stream_thread_priority" class="form-select" v-model="config.stream_thread_priority">
        <option value="off">{{ $t('config.stream_thread_priority_off') }}</option>
        <option value="nice">{{ $t('config.stream_thread_priority_nice') }}</option>
        <option value="realtime">{{ $t('config.stream_thread_priority_realtime') }}</option>
      </select>
      <div class="form-text">{{ $t('config.stream_thread_priority_desc') }}</div>
    </div>
    <div class="mb-3" v-if="platform === 'linux'">
      <label for="gamescope_stream_poll" class="form-label">{{ $t('config.gamescope_stream_poll') }}</label>
      <select id="gamescope_stream_poll" class="form-select" v-model="config.gamescope_stream_poll">
        <option value="timer">{{ $t('config.gamescope_stream_poll_timer') }}</option>
        <option value="events">{{ $t('config.gamescope_stream_poll_events') }}</option>
      </select>
      <div class="form-text">{{ $t('config.gamescope_stream_poll_desc') }}</div>
    </div>
    <div class="mb-3 form-check" v-if="platform === 'linux'">
      <input type="checkbox" class="form-check-input" id="gamescope_stream_hdr_efc" v-model="config.gamescope_stream_hdr_efc" true-value="enabled" false-value="disabled" />
      <label for="gamescope_stream_hdr_efc" class="form-check-label">{{ $t('config.gamescope_stream_hdr_efc') }}</label>
      <div class="form-text">{{ $t('config.gamescope_stream_hdr_efc_desc') }}</div>
    </div>

    <!-- Quantization Parameter -->
    <div class="mb-3">
      <label for="qp" class="form-label">{{ $t('config.qp') }}</label>
      <input type="number" class="form-control" id="qp" placeholder="28" v-model="config.qp" />
      <div class="form-text">{{ $t('config.qp_desc') }}</div>
    </div>

    <!-- Min Threads -->
    <div class="mb-3">
      <label for="min_threads" class="form-label">{{ $t('config.min_threads') }}</label>
      <input type="number" class="form-control" id="min_threads" placeholder="2" min="1" v-model="config.min_threads" />
      <div class="form-text">{{ $t('config.min_threads_desc') }}</div>
    </div>

    <!-- Limit Framerate -->
    <Checkbox class="mb-3"
              id="limit_framerate"
              locale-prefix="config"
              v-model="config.limit_framerate"
              default="true"
    ></Checkbox>

    <!-- ENVVAR compatibility mode -->
    <Checkbox class="mb-3"
              id="envvar_compatibility_mode"
              locale-prefix="config"
              v-model="config.envvar_compatibility_mode"
              default="false"
    ></Checkbox>

    <!-- Legacy ordering -->
    <Checkbox class="mb-3"
              id="legacy_ordering"
              locale-prefix="config"
              v-model="config.legacy_ordering"
              default="false"
    ></Checkbox>

    <!-- Ignore Encoder Probe Failure -->
    <Checkbox class="mb-3"
              id="ignore_encoder_probe_failure"
              locale-prefix="config"
              v-model="config.ignore_encoder_probe_failure"
              default="false"
    ></Checkbox>

    <!-- Experimental PyroWave codec -->
    <Checkbox class="mb-3"
              id="pyrowave"
              locale-prefix="config"
              v-model="config.pyrowave"
              default="false"
    ></Checkbox>

    <!-- HEVC Support -->
    <div class="mb-3">
      <label for="hevc_mode" class="form-label">{{ $t('config.hevc_mode') }}</label>
      <select id="hevc_mode" class="form-select" v-model="config.hevc_mode">
        <option value="0">{{ $t('config.hevc_mode_0') }}</option>
        <option value="1">{{ $t('config.hevc_mode_1') }}</option>
        <option value="2">{{ $t('config.hevc_mode_2') }}</option>
        <option value="3">{{ $t('config.hevc_mode_3') }}</option>
      </select>
      <div class="form-text">{{ $t('config.hevc_mode_desc') }}</div>
    </div>

    <!-- AV1 Support -->
    <div class="mb-3">
      <label for="av1_mode" class="form-label">{{ $t('config.av1_mode') }}</label>
      <select id="av1_mode" class="form-select" v-model="config.av1_mode">
        <option value="0">{{ $t('config.av1_mode_0') }}</option>
        <option value="1">{{ $t('config.av1_mode_1') }}</option>
        <option value="2">{{ $t('config.av1_mode_2') }}</option>
        <option value="3">{{ $t('config.av1_mode_3') }}</option>
      </select>
      <div class="form-text">{{ $t('config.av1_mode_desc') }}</div>
    </div>

    <!-- Capture -->
    <div class="mb-3" v-if="platform !== 'macos'">
      <label for="capture" class="form-label">{{ $t('config.capture') }}</label>
      <select id="capture" class="form-select" v-model="config.capture">
        <option value="">{{ $t('_common.autodetect') }}</option>
        <PlatformLayout :platform="platform">
          <template #linux>
            <option value="nvfbc">NvFBC</option>
            <option value="wlr">wlroots</option>
            <option value="kms">KMS</option>
            <option value="x11">X11</option>
          </template>
          <template #windows>
            <option value="ddx">Desktop Duplication API</option>
            <option value="wgc">Windows.Graphics.Capture {{ $t('_common.beta') }}</option>
          </template>
        </PlatformLayout>
      </select>
      <div class="form-text">{{ $t('config.capture_desc') }}</div>
    </div>

    <!-- Encoder -->
    <div class="mb-3">
      <label for="encoder" class="form-label">{{ $t('config.encoder') }}</label>
      <select id="encoder" class="form-select" v-model="config.encoder">
        <option value="">{{ $t('_common.autodetect') }}</option>
        <PlatformLayout :platform="platform">
          <template #windows>
            <option value="nvenc">NVIDIA NVENC</option>
            <option value="quicksync">Intel QuickSync</option>
            <option value="amdvce">AMD AMF/VCE</option>
          </template>
          <template #linux>
            <option value="nvenc">NVIDIA NVENC</option>
            <option value="vaapi">VA-API</option>
          </template>
          <template #macos>
            <option value="videotoolbox">VideoToolbox</option>
          </template>
        </PlatformLayout>
        <option value="software">{{ $t('config.encoder_software') }}</option>
      </select>
      <div class="form-text">{{ $t('config.encoder_desc') }}</div>
    </div>

  </div>
</template>

<style scoped>

</style>
