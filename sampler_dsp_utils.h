/**
 * =====================================================================
 * sampler_dsp_utils.h - ARM CMSIS-DSP Optimized Operations
 * =====================================================================
 * Utilidades de DSP optimizadas usando instrucciones SIMD del ARM Cortex-M7.
 * Aprovecha CMSIS-DSP para operaciones de audio de alta performance.
 * 
 * IMPORTANTE: Para usar este archivo, asegúrate de:
 * 1. Incluir CMSIS-DSP en tu proyecto (viene con STM32Cube)
 * 2. Agregar -DARM_MATH_CM7 al compilador
 * 3. Linkar con libarm_cortexM7lfdp_math.a
 */

#ifndef SAMPLER_DSP_UTILS_H
#define SAMPLER_DSP_UTILS_H

#include <stdint.h>
#include <stddef.h>

// Verificar si CMSIS-DSP está disponible
#ifdef ARM_MATH_CM7
  #include "arm_math.h"
  #define USE_CMSIS_DSP 1
#else
  #define USE_CMSIS_DSP 0
  #warning "CMSIS-DSP not available, using slower fallback implementations"
#endif

namespace crearttech {

/**
 * @brief Clase con utilidades DSP optimizadas para operaciones de audio.
 */
class DSPUtils {
public:

  /**
   * @brief Mezcla dos buffers con ganancia (dest = dest + src * gain).
   * @param dest Buffer destino (será modificado in-place)
   * @param src Buffer fuente
   * @param length Número de muestras
   * @param gain Ganancia a aplicar a src antes de mezclar
   */
  static void MixBuffersWithGain(float* dest, const float* src, size_t length, float gain) {
    #if USE_CMSIS_DSP
      float temp_buffer[length];
      

      arm_scale_f32(src, gain, temp_buffer, static_cast<uint32_t>(length));
      

      arm_add_f32(dest, temp_buffer, dest, static_cast<uint32_t>(length));
    #else
      for (size_t i = 0; i < length; i++) {
        dest[i] += src[i] * gain;
      }
    #endif
  }

  /**
   * @brief Copia buffer con ganancia (dest = src * gain).
   * @param dest Buffer destino
   * @param src Buffer fuente
   * @param length Número de muestras
   * @param gain Ganancia a aplicar
   */
  static void CopyWithGain(float* dest, const float* src, size_t length, float gain) {
    #if USE_CMSIS_DSP
      arm_scale_f32(src, gain, dest, static_cast<uint32_t>(length));
    #else
      for (size_t i = 0; i < length; i++) {
        dest[i] = src[i] * gain;
      }
    #endif
  }

  /**
   * @brief Aplica fade in/out lineal a un buffer.
   * @param buffer Buffer a procesar (in-place)
   * @param length Número de muestras
   * @param fade_in true para fade in, false para fade out
   */
  static void ApplyLinearFade(float* buffer, size_t length, bool fade_in) {
    if (length == 0) return;
    
    for (size_t i = 0; i < length; i++) {
      float fade_factor = static_cast<float>(i) / static_cast<float>(length - 1);
      if (!fade_in) {
        fade_factor = 1.0f - fade_factor;
      }
      buffer[i] *= fade_factor;
    }
  }

  /**
   * @brief Calcula el valor RMS (Root Mean Square) de un buffer.
   * @param buffer Buffer de audio
   * @param length Número de muestras
   * @return Valor RMS
   */
  static float CalculateRMS(const float* buffer, size_t length) {
    if (length == 0) return 0.0f;
    
    #if USE_CMSIS_DSP
      float result;
      arm_rms_f32(buffer, static_cast<uint32_t>(length), &result);
      return result;
    #else
      float sum_squares = 0.0f;
      for (size_t i = 0; i < length; i++) {
        sum_squares += buffer[i] * buffer[i];
      }
      return sqrtf(sum_squares / static_cast<float>(length));
    #endif
  }

  /**
   * @brief Encuentra el valor máximo absoluto en un buffer.
   * @param buffer Buffer de audio
   * @param length Número de muestras
   * @return Máximo valor absoluto
   */
  static float FindPeak(const float* buffer, size_t length) {
    if (length == 0) return 0.0f;
    
    #if USE_CMSIS_DSP
      float max_val;
      uint32_t max_index;
      arm_absmax_f32(buffer, static_cast<uint32_t>(length), &max_val, &max_index);
      return max_val;
    #else
      float peak = 0.0f;
      for (size_t i = 0; i < length; i++) {
        float abs_val = fabsf(buffer[i]);
        if (abs_val > peak) {
          peak = abs_val;
        }
      }
      return peak;
    #endif
  }

  /**
   * @brief Limpia un buffer (pone todos los valores a 0).
   * @param buffer Buffer a limpiar
   * @param length Número de muestras
   */
  static void ClearBuffer(float* buffer, size_t length) {
    #if USE_CMSIS_DSP
      arm_fill_f32(0.0f, buffer, static_cast<uint32_t>(length));
    #else
      memset(buffer, 0, length * sizeof(float));
    #endif
  }

  /**
   * @brief Soft clipping vectorizado usando tanh (más rápido que loop manual).
   * @param buffer Buffer a procesar (in-place)
   * @param length Número de muestras
   * @param threshold Umbral antes de aplicar clipping
   */
  static void ApplySoftClipping(float* buffer, size_t length, float threshold = 0.7f) {
    const float inv_threshold = 1.0f / threshold;
    
    for (size_t i = 0; i < length; i++) {
      if (fabsf(buffer[i]) > threshold) {
        buffer[i] = tanhf(buffer[i] * threshold) * inv_threshold;
      }
    }
  }

  /**
   * @brief Crossfade entre dos buffers (mezcla suave).
   * @param bufferA Primer buffer (peso inicial 100%)
   * @param bufferB Segundo buffer (peso inicial 0%)
   * @param dest Buffer destino
   * @param length Número de muestras
   */
  static void Crossfade(const float* bufferA, const float* bufferB, float* dest, size_t length) {
    if (length == 0) return;
    
    for (size_t i = 0; i < length; i++) {
      float fade = static_cast<float>(i) / static_cast<float>(length - 1);
      dest[i] = bufferA[i] * (1.0f - fade) + bufferB[i] * fade;
    }
  }

  /**
   * @brief Interpolación lineal optimizada para resampling.
   * @param buffer Buffer fuente
   * @param length Longitud del buffer
   * @param position Posición flotante (0.0 a length-1)
   * @return Valor interpolado
   */
  static inline float LinearInterpolate(const float* buffer, size_t length, float position) {
    if (position >= length - 1) return buffer[length - 1];
    if (position <= 0) return buffer[0];
    
    size_t index = static_cast<size_t>(position);
    float frac = position - static_cast<float>(index);
    
    return buffer[index] + frac * (buffer[index + 1] - buffer[index]);
  }
};

} // namespace crearttech

#endif // SAMPLER_DSP_UTILS_H
