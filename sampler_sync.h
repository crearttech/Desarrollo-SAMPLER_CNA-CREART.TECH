/**
 * =====================================================================
 * sampler_sync.h - Clock Synchronization and Tempo Management
 * =====================================================================
 * Maneja sincronización de tempo (BPM), time signature, y alineación de beats.
 * Útil para sincronización MIDI y quantización musical precisa.
 */

#ifndef SAMPLER_SYNC_H
#define SAMPLER_SYNC_H

#include <stdint.h>
#include <stddef.h>

namespace crearttech {

/**
 * @brief Clase para sincronización de tempo y clock.
 */
class ClockSync {
public:
  ClockSync() 
    : _bpm(120.0f)
    , _time_sig_numerator(4)
    , _time_sig_denominator(4)
    , _sample_rate(48000.0f)
    , _samples_per_beat(0)
    , _samples_per_bar(0)
    , _sample_counter(0)
    , _beat_counter(0)
  {
    CalculateTimings();
  }

  /**
   * @brief Configura el tempo en BPM.
   * @param bpm Beats por minuto (ej: 120.0)
   */
  void SetBPM(float bpm) {
    if (bpm <= 0.0f) return;
    _bpm = bpm;
    CalculateTimings();
  }

  /**
   * @brief Configura el sample rate del sistema.
   * @param sample_rate Sample rate en Hz (ej: 48000)
   */
  void SetSampleRate(float sample_rate) {
    if (sample_rate <= 0.0f) return;
    _sample_rate = sample_rate;
    CalculateTimings();
  }

  /**
   * @brief Configura la signatura de tiempo.
   * @param numerator Numerador (ej: 4 para 4/4)
   * @param denominator Denominador (ej: 4 para 4/4)
   */
  void SetTimeSignature(uint8_t numerator, uint8_t denominator) {
    if (numerator == 0 || denominator == 0) return;
    _time_sig_numerator = numerator;
    _time_sig_denominator = denominator;
    CalculateTimings();
  }

  /**
   * @brief Avanza el contador de muestras (llamar en cada muestra de audio).
   */
  void Tick() {
    _sample_counter++;
    
    // Verificar si hemos llegado a un nuevo beat
    if (_samples_per_beat > 0 && _sample_counter >= _samples_per_beat) {
      _sample_counter = 0;
      _beat_counter++;
      
      // Reset beat counter al inicio de un bar
      if (_beat_counter >= _time_sig_numerator) {
        _beat_counter = 0;
      }
    }
  }

  /**
   * @brief Verifica si debemos disparar una acción en el siguiente beat.
   * @return true si estamos en el inicio de un beat
   */
  bool ShouldTriggerOnBeat() {
    // Estamos en un beat si el contador de muestras está cerca de 0
    // Usamos un umbral pequeño para compensar timing imperfecto
    const size_t BEAT_THRESHOLD = 10; // ~0.2ms @ 48kHz
    return (_sample_counter < BEAT_THRESHOLD);
  }

  /**
   * @brief Verifica si estamos en el downbeat (primer beat del compás).
   */
  bool IsDownbeat() {
    return (_beat_counter == 0 && ShouldTriggerOnBeat());
  }

  /**
   * @brief Calcula longitud alineada a beats más cercana.
   * @param samples Número de muestras sin procesar
   * @return Número de muestras alineado al beat más cercano
   */
  size_t GetBeatAlignedLength(size_t samples) {
    if (_samples_per_beat == 0) return samples;
    
    // Redondear al beat más cercano
    size_t num_beats = (samples + _samples_per_beat / 2) / _samples_per_beat;
    return num_beats * _samples_per_beat;
  }

  /**
   * @brief Calcula longitud alineada a un número específico de beats.
   * @param samples Número de muestras sin procesar
   * @param beat_count Número de beats deseado
   * @return Número de muestras para exactamente beat_count beats
   */
  size_t GetExactBeatLength(size_t beat_count) {
    return beat_count * _samples_per_beat;
  }

  /**
   * @brief Calcula el beat más cercano para un timestamp dado.
   * @param sample_position Posición en muestras
   * @return Posición alineada al beat más cercano
   */
  size_t SnapToNearestBeat(size_t sample_position) {
    if (_samples_per_beat == 0) return sample_position;
    
    size_t beat_number = (sample_position + _samples_per_beat / 2) / _samples_per_beat;
    return beat_number * _samples_per_beat;
  }

  /**
   * @brief Obtiene el BPM actual.
   */
  float GetBPM() const { return _bpm; }

  /**
   * @brief Obtiene muestras por beat.
   */
  size_t GetSamplesPerBeat() const { return _samples_per_beat; }

  /**
   * @brief Obtiene muestras por compás completo.
   */
  size_t GetSamplesPerBar() const { return _samples_per_bar; }

  /**
   * @brief Resetea contadores (útil al iniciar grabación).
   */
  void Reset() {
    _sample_counter = 0;
    _beat_counter = 0;
  }

private:
  /**
   * @brief Calcula timings internos basados en BPM y sample rate.
   */
  void CalculateTimings() {
    // Samples per beat = (sample_rate * 60) / bpm
    _samples_per_beat = static_cast<size_t>((_sample_rate * 60.0f) / _bpm);
    
    // Samples per bar = samples_per_beat * beats_per_bar
    _samples_per_bar = _samples_per_beat * _time_sig_numerator;
  }

  float _bpm;                    // Tempo en beats por minuto
  uint8_t _time_sig_numerator;   // Numerador de signatura (4 en 4/4)
  uint8_t _time_sig_denominator; // Denominador de signatura (4 en 4/4)
  float _sample_rate;            // Sample rate del sistema
  
  size_t _samples_per_beat;      // Muestras en un beat
  size_t _samples_per_bar;       // Muestras en un compás completo
  
  size_t _sample_counter;        // Contador de muestras desde último beat
  uint8_t _beat_counter;         // Beat actual en el compás (0 a numerator-1)
};

} // namespace crearttech

#endif // SAMPLER_SYNC_H
