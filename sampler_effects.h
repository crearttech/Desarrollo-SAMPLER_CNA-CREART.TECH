/**
 * =====================================================================
 * sampler_effects.h - Audio Effects Processing Module
 * =====================================================================
 * Maneja todos los efectos de audio aplicados al looper:
 * - Reproducción reversa
 * - Pitch shifting
 * - Filtros (highpass, lowpass, bandpass)
 * - Delay y reverb (futuro)
 */

#ifndef SAMPLER_EFFECTS_H
#define SAMPLER_EFFECTS_H

#include <stdint.h>
#include <stddef.h>
#include <cmath>

namespace crearttech {

enum FilterType {
  FILTER_NONE,
  FILTER_LOWPASS,
  FILTER_HIGHPASS,
  FILTER_BANDPASS
};

/**
 * @brief Clase para procesar efectos de audio sobre loops.
 */
class LoopEffects {
public:
  LoopEffects() : _pitch_semitones(0.0f), _filter_type(FILTER_NONE), _filter_cutoff(1000.0f) {}

  /**
   * @brief Procesa reproducción reversa del buffer.
   * @param buffer Puntero al buffer de audio
   * @param playhead Posición actual del cabezal (será modificada)
   * @param length Longitud del loop
   * @return Muestra actual procesada en reversa
   */
  float ProcessReverse(float* buffer, float& playhead, size_t length) {
    if (length == 0) return 0.0f;
    
    // Leer desde el final hacia el inicio
    size_t reversed_pos = length - 1 - static_cast<size_t>(playhead);
    
    // Manejar wrap-around
    if (reversed_pos >= length) reversed_pos = 0;
    
    return buffer[reversed_pos];
  }

  /**
   * @brief Aplica pitch shift a una muestra individual.
   * @param sample Muestra de entrada
   * @param semitones Cambio de pitch en semitonos (-12 a +12)
   * @return Muestra con pitch ajustado
   */
  float ProcessPitchShift(float sample, float semitones) {
    float pitch_ratio = powf(2.0f, semitones / 12.0f);
    return sample * pitch_ratio;
  }

  /**
   * @brief Configura el tipo de filtro a aplicar.
   */
  void SetFilterType(FilterType type) { _filter_type = type; }

  /**
   * @brief Configura la frecuencia de corte del filtro.
   */
  void SetFilterCutoff(float cutoff_hz) { 
    _filter_cutoff = cutoff_hz; 
  }

  /**
   * @brief Aplica filtro a un buffer completo.
   * @param buffer Buffer de audio a filtrar (in-place)
   * @param length Longitud del buffer
   * @param type Tipo de filtro a aplicar
   */
  void ApplyFilter(float* buffer, size_t length, FilterType type) {
    if (type == FILTER_NONE || length == 0) return;
    
    if (type == FILTER_LOWPASS) {
      float alpha = 0.1f; // Factor de suavizado
      for (size_t i = 1; i < length; i++) {
        buffer[i] = buffer[i] * alpha + buffer[i-1] * (1.0f - alpha);
      }
    }
  }

  /**
   * @brief Procesa una muestra con todos los efectos configurados.
   * @param sample Muestra de entrada
   * @return Muestra procesada con efectos
   */
  float ProcessSample(float sample) {
    float output = sample;
    
    // Aplicar pitch shift si está configurado
    if (_pitch_semitones != 0.0f) {
      output = ProcessPitchShift(output, _pitch_semitones);
    }
    
    return output;
  }

private:
  float _pitch_semitones;
  FilterType _filter_type;
  float _filter_cutoff;
};

} // namespace crearttech

#endif // SAMPLER_EFFECTS_H
