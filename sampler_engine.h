#pragma once
// SAMPLER CNA - Audio Engine
#include <string.h>

namespace crearttech {

  /**
   * @class OverdubLooper
   * @brief Motor principal del looper con grabación, reproducción, overdub y undo/redo.
   */
class OverdubLooper {
public:
  /**
   * @brief Prepara el looper para su uso.
   * @param buf Puntero a un búfer de memoria (ej. en la SDRAM) donde se guardará el audio.
   * @param length La longitud total de ese búfer en número de muestras.
   * @param undo_bufs Array de punteros a buffers de undo (opcional, puede ser nullptr)
   * @param num_undo_levels Número de niveles de undo disponibles (max MAX_UNDO_LEVELS)
   */
  void Init(float *buf, size_t length, float** undo_bufs = nullptr, size_t num_undo_levels = 0) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(buf);
    if (addr % 32 != 0) {
      #ifdef DEBUG
      Serial.println("WARNING: SDRAM buffer not 32-byte aligned.");
      #endif
    }
    
    _buffer = buf;
    _buffer_length = length;
    
    // Configurar buffers de undo si se proporcionan
    if (undo_bufs != nullptr && num_undo_levels > 0) {
      _undo_enabled = true;
      _undo_count = (num_undo_levels <= MAX_UNDO_LEVELS) ? num_undo_levels : MAX_UNDO_LEVELS;
      for (size_t i = 0; i < _undo_count; i++) {
        _undo_buffers[i] = undo_bufs[i];
      }
    } else {
      _undo_enabled = false;
      _undo_count = 0;
    }
    
    memset(_buffer, 0, sizeof(float) * _buffer_length);
    

    _inv_buffer_length = 1.0f / static_cast<float>(_buffer_length);
    _inv_crossfade_samples = 1.0f / static_cast<float>(CROSSFADE_SAMPLES);
    
    _is_empty = true;
    _reverse = false;
    _playback_speed = 1.0f;
  }

  // --- Funciones de Control de Estado ---

  /** @brief Inicia la grabación desde el principio del búfer. */
  void StartRecording() {
    _rec_head = 0;
    _play_head = 0.0f;
    _is_empty = false;
    _is_recording = true;
    _overdubbing = false;
  }

  /** @brief Detiene la grabación actual y aplica crossfade para transiciones suaves. */
  void StopRecording() { 
    _is_recording = false;
    ApplyCrossfade();
  }

  /** @brief Inicia la sobregrabación (mezcla la entrada con lo que ya hay). */
  void StartOverdub()  { 
    SaveUndoState();
    _overdubbing = true; 
  }

  /** @brief Detiene la sobregrabación. */
  void StopOverdub()   { _overdubbing = false; }

  /** @brief Vuelve a colocar el cabezal de reproducción al inicio del loop. */
  void Restart()       { _play_head = 0; }

  // --- Funciones de Manipulación del Loop ---

  /**
   * @brief Define la sección del búfer que se usará para el loop (obsoleto, usar SetLoopRegion).
   * @param loop_start Posición de inicio normalizada (0.0 a 1.0).
   * @param loop_length Longitud del loop normalizada (0.0 a 1.0).
   */
  void SetLoop(const float loop_start, const float loop_length) {
    _loop_start = static_cast<size_t>(loop_start * (_buffer_length - 1));
    _loop_length = static_cast<size_t>(loop_length * _buffer_length);
    if (_loop_length < 1) _loop_length = 1;
  }

  /**
   * @brief Define la región del loop usando posiciones de muestra exactas.
   * @param start_sample Muestra de inicio.
   * @param end_sample Muestra de fin.
   */
  void SetLoopRegion(size_t start_sample, size_t end_sample) {
      _loop_start = start_sample;
      _loop_length = end_sample - start_sample + 1;
      if (_loop_length < 1) _loop_length = 1;
      if (_play_head >= _loop_length) {
          _play_head = 0;
      }
  }

  /** @brief Activa o desactiva la reproducción en reversa. */
  void SetReverse(bool reverse) { _reverse = reverse; }

  /** @brief Ajusta la velocidad de reproducción. 1.0 es normal, >1.0 es más rápido. */
  void SetPlaybackSpeed(float speed) { _playback_speed = speed; }
  
  /**
   * @brief Configura el tempo para quantización basada en BPM.
   * @param bpm Beats por minuto (ej: 120.0)
   * @param sample_rate Sample rate del sistema (ej: 48000)
   */
  void SetTempo(float bpm, float sample_rate) {
    if (bpm <= 0.0f || sample_rate <= 0.0f) return;
    _bpm = bpm;
    _samples_per_beat = static_cast<size_t>((sample_rate * 60.0f) / bpm);
  }
  
  /**
   * @brief Activa o desactiva la quantización rítmica.
   * @param enable true para activar quantización, false para desactivar
   * @param beats Número de beats a los que quantizar (por defecto 4)
   */
  void SetQuantize(bool enable, size_t beats = 4) {
    _quantize = enable;
    _quantize_beats = (beats > 0) ? beats : 4;
  }
  
  /**
   * @brief Quantiza la longitud grabada al número de beats configurado (método simple).
   * @param recorded_length Longitud grabada original en muestras
   * @return Longitud quantizada en muestras
   */
  size_t QuantizeLength(size_t recorded_length) {
    if (!_quantize || _quantize_beats == 0) return recorded_length;
    

    size_t beat_length = recorded_length / _quantize_beats;
    

    return beat_length * _quantize_beats;
  }
  
  /**
   * @brief Quantiza una región del loop completa (inicio y final) basado en BPM.
   * @param start_sample Posición de inicio grabada (sample absoluto desde que empezó el sistema)
   * @param end_sample Posición de fin grabada (sample absoluto)
   * @param out_start Referencia donde se escribirá el inicio quantizado
   * @param out_end Referencia donde se escribirá el fin quantizado
   */
  void QuantizeLoopRegion(size_t start_sample, size_t end_sample, size_t& out_start, size_t& out_end) {
    if (!_quantize || _samples_per_beat == 0) {
      // Sin quantización, devolver valores originales
      out_start = start_sample;
      out_end = end_sample;
      return;
    }
    
    // Ajustar inicio al beat más cercano
    size_t start_beat = (start_sample + _samples_per_beat / 2) / _samples_per_beat;
    out_start = start_beat * _samples_per_beat;
    
    // Calcular longitud en muestras
    size_t recorded_length = (end_sample > start_sample) ? (end_sample - start_sample) : 0;
    
    // Calcular longitud en beats y redondear
    size_t length_in_beats = (recorded_length + _samples_per_beat / 2) / _samples_per_beat;
    
    // Asegurar que tengamos al menos la cantidad de beats requeridos
    if (length_in_beats < _quantize_beats) {
      length_in_beats = _quantize_beats;
    } else {
      // Redondear al múltiplo de _quantize_beats más cercano
      length_in_beats = ((length_in_beats + _quantize_beats / 2) / _quantize_beats) * _quantize_beats;
    }
    
    // Calcular final basado en el inicio quantizado + longitud quantizada
    out_end = out_start + (length_in_beats * _samples_per_beat);
  }

  
  /**
   * @brief Guarda el estado actual del buffer en el historial de undo.
   * Llamar antes de operaciones destructivas como overdub o ediciones.
   */
  void SaveUndoState() {
    if (!_undo_enabled || _undo_count == 0) return;
    

    memcpy(_undo_buffers[_undo_write_index], 
           _buffer + _loop_start, 
           sizeof(float) * _loop_length);
    

    _undo_write_index = (_undo_write_index + 1) % _undo_count;
    

    _undo_read_index = _undo_write_index;
    

    if (_undo_depth < _undo_count) {
      _undo_depth++;
    }
  }
  
  /**
   * @brief Deshace la última operación, restaurando el estado anterior.
   * @return true si se pudo deshacer, false si no hay más niveles de undo
   */
  bool Undo() {
    if (!_undo_enabled || _undo_depth == 0) return false;
    

    _undo_read_index = (_undo_read_index - 1 + _undo_count) % _undo_count;
    

    memcpy(_buffer + _loop_start, 
           _undo_buffers[_undo_read_index], 
           sizeof(float) * _loop_length);
    

    _undo_depth--;
    

    if (_redo_depth < MAX_UNDO_LEVELS - 1) {
      _redo_depth++;
    }
    
    return true;
  }
  
  /**
   * @brief Rehace una operación previamente deshecha.
   * @return true si se pudo rehacer, false si no hay más niveles de redo
   */
  bool Redo() {
    if (!_undo_enabled || _redo_depth == 0) return false;
    

    _undo_read_index = (_undo_read_index + 1) % _undo_count;
    

    memcpy(_buffer + _loop_start, 
           _undo_buffers[_undo_read_index], 
           sizeof(float) * _loop_length);
    

    _undo_depth++;
    

    _redo_depth--;
    
    return true;
  }
  
  /**
   * @brief Verifica si hay niveles de undo disponibles.
   */
  bool CanUndo() const { return _undo_enabled && _undo_depth > 0; }
  
  /**
   * @brief Verifica si hay niveles de redo disponibles.
   */
  bool CanRedo() const { return _undo_enabled && _redo_depth > 0; }

  // --- Funciones de Obtención de Estado ---
  /**
   * @brief Devuelve la posición actual del cabezal de reproducción (normalizada 0.0 a 1.0). 
   */
  float GetPlayhead() {
    return static_cast<float>(_loop_start + _play_head) * _inv_buffer_length;
  }

  /** @brief Devuelve la posición del cabezal dentro de la región del loop (en muestras). */
  size_t GetLoopPlayheadPosition() const { return static_cast<size_t>(_play_head); }

  /**
   * @brief Procesa una única muestra de audio.
   * @param in Muestra de audio de entrada (ADC).
   * @return Muestra de audio de salida (DAC).
   */
  float Process(float in) {

    if (_is_recording) {
      _buffer[_rec_head] = in;

      _rec_head++;
      if (_rec_head >= _buffer_length) {
        _rec_head = 0;
        _is_recording = false;
      }
      return in;
    }

    if (_is_empty) return 0.0f;

    float out = GetInterpolatedSample(_play_head);

    if (_overdubbing) {
      size_t index = (_loop_start + static_cast<size_t>(_play_head)) % _buffer_length;
      float mixed = _buffer[index] + in;
      mixed = SoftClip(mixed);
      _buffer[index] = mixed;
      out = mixed;
    }

    if (_reverse) {
      _play_head -= _playback_speed;
      if (_play_head < 0.0f) _play_head += _loop_length;
    } else {
      _play_head += _playback_speed;
      if (_play_head >= _loop_length) _play_head -= _loop_length;
    }

    return out;
  }

private:
  // --- Constantes ---
  static const size_t CROSSFADE_SAMPLES = 128; // ~2.7ms @ 48kHz
  

  
  /**
   * @brief Aplica crossfade en los límites del loop para evitar clicks.
   */
  void ApplyCrossfade() {

    if (_loop_length < CROSSFADE_SAMPLES * 2) return;
    
    for (size_t i = 0; i < CROSSFADE_SAMPLES; i++) {
      float fade = static_cast<float>(i) * _inv_crossfade_samples;
      
      size_t start_idx = (_loop_start + i) % _buffer_length;
      size_t end_idx = (_loop_start + _loop_length - CROSSFADE_SAMPLES + i) % _buffer_length;

      _buffer[start_idx] = _buffer[start_idx] * fade + 
                           _buffer[end_idx] * (1.0f - fade);
    }
  }
  
  /**
   * @brief Soft clipping con tanh.
   */
  float SoftClip(float x) {
    return tanhf(x * 0.7f) / 0.7f;
  }
  
  /**
   * @brief Obtiene una muestra interpolada del buffer para reproducción suave.
   * @param position Posición flotante dentro del loop (0.0 a _loop_length)
   * @return Muestra interpolada linealmente entre dos muestras adyacentes
   */
  float GetInterpolatedSample(float position) {
    size_t idx0 = static_cast<size_t>(position);
    size_t idx1 = (idx0 + 1) % _loop_length;
    float frac = position - static_cast<float>(idx0);
    size_t actual_idx0 = (_loop_start + idx0) % _buffer_length;
    size_t actual_idx1 = (_loop_start + idx1) % _buffer_length;
    return _buffer[actual_idx0] * (1.0f - frac) + _buffer[actual_idx1] * frac;
  }

  // --- Variables Miembro (Estado Interno) ---

  float* _buffer;

  size_t _buffer_length;
  size_t _loop_start;
  size_t _loop_length;
  
  float  _play_head;
  size_t _rec_head;

  bool _is_empty;
  bool _is_recording;
  bool _reverse;
  bool _overdubbing;

  float _playback_speed;
  
  // Quantización rítmica
  bool _quantize = false;
  size_t _quantize_beats = 4;
  float _bpm = 120.0f;
  size_t _samples_per_beat = 0;
  
  float _inv_buffer_length = 0.0f;
  float _inv_crossfade_samples = 0.0f;
  
  static const size_t MAX_UNDO_LEVELS = 3;
  float* _undo_buffers[MAX_UNDO_LEVELS];
  bool _undo_enabled = false;
  size_t _undo_count = 0;
  size_t _undo_write_index = 0;
  size_t _undo_read_index = 0;
  size_t _undo_depth = 0;
  size_t _redo_depth = 0;
};

} // namespace crearttech