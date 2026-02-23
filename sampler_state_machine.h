/**
 * =====================================================================
 * sampler_state_machine.h - Looper State Machine
 * =====================================================================
 * Máquina de estados robusta con validación de transiciones y callbacks.
 * Previene transiciones inválidas y centraliza la lógica de estado.
 */

#ifndef SAMPLER_STATE_MACHINE_H
#define SAMPLER_STATE_MACHINE_H

#include <stdint.h>

namespace crearttech {

/**
 * @brief Estados posibles del looper (type-safe con enum class)
 */
enum class LooperState : uint8_t {
  IDLE,              // Sin loop grabado, esperando acción
  RECORDING_INITIAL, // Grabando el primer loop
  PLAYING,           // Reproduciendo loop
  OVERDUBBING,       // Agregando capas al loop existente
  PAUSED             // Pausado (mantiene posición)
};

/**
 * @brief Eventos que pueden causar transiciones de estado
 */
enum class LooperEvent : uint8_t {
  PRESS_REC,         // Botón REC presionado
  RELEASE_REC,       // Botón REC soltado
  PRESS_PLAY,        // Botón PLAY presionado
  PRESS_STOP,        // Botón STOP presionado
  PRESS_PAUSE,       // Botón PAUSE presionado
  LOOP_ENDED,        // El loop llegó al final naturalmente
  CLEAR_LOOP         // Borrar loop actual
};

/**
 * @brief Máquina de estados para el looper
 */
class LooperStateMachine {
public:
  LooperStateMachine() : _current_state(LooperState::IDLE), _previous_state(LooperState::IDLE) {}

  /**
   * @brief Obtiene el estado actual.
   */
  LooperState GetState() const { return _current_state; }

  /**
   * @brief Obtiene el estado anterior.
   */
  LooperState GetPreviousState() const { return _previous_state; }

  /**
   * @brief Verifica si una transición es válida.
   */
  bool CanTransition(LooperState from, LooperState to) const {
    // Siempre se puede ir a IDLE (reset/stop)
    if (to == LooperState::IDLE) return true;
    
    // Tabla de transiciones válidas
    switch (from) {
      case LooperState::IDLE:
        // Desde IDLE solo puedes empezar a grabar
        return (to == LooperState::RECORDING_INITIAL);
        
      case LooperState::RECORDING_INITIAL:
        // Desde grabación inicial puedes ir a PLAYING o IDLE
        return (to == LooperState::PLAYING || to == LooperState::IDLE);
        
      case LooperState::PLAYING:
        // Desde PLAYING puedes hacer overdub, pausar, o parar
        return (to == LooperState::OVERDUBBING || 
                to == LooperState::PAUSED || 
                to == LooperState::IDLE);
        
      case LooperState::OVERDUBBING:
        // Desde OVERDUB puedes volver a PLAYING o parar
        return (to == LooperState::PLAYING || 
                to == LooperState::IDLE);
        
      case LooperState::PAUSED:
        // Desde PAUSED puedes resumir o parar
        return (to == LooperState::PLAYING || 
                to == LooperState::IDLE);
        
      default:
        return false;
    }
  }

  /**
   * @brief Procesa un evento y transiciona si es válido.
   * @return true si la transición ocurrió, false si no fue válida
   */
  bool ProcessEvent(LooperEvent event) {
    LooperState new_state = GetNextState(_current_state, event);
    
    if (new_state == _current_state) {
      // No hay cambio de estado
      return false;
    }
    
    return TransitionTo(new_state);
  }

  /**
   * @brief Transiciona a un nuevo estado si es válido.
   * @return true si la transición ocurrió, false si fue bloqueada
   */
  bool TransitionTo(LooperState new_state) {
    if (!CanTransition(_current_state, new_state)) {
      // Transición no válida, bloquear
      return false;
    }
    
    // Ejecutar callback de salida del estado actual
    OnExit(_current_state);
    
    // Actualizar estados
    _previous_state = _current_state;
    _current_state = new_state;
    
    // Ejecutar callback de entrada al nuevo estado
    OnEnter(new_state);
    
    return true;
  }

  /**
   * @brief Verifica si estamos en un estado específico.
   */
  bool IsInState(LooperState state) const {
    return _current_state == state;
  }

  /**
   * @brief Verifica si el looper está activo (no idle).
   */
  bool IsActive() const {
    return _current_state != LooperState::IDLE;
  }

  /**
   * @brief Verifica si estamos grabando (inicial u overdub).
   */
  bool IsRecording() const {
    return (_current_state == LooperState::RECORDING_INITIAL || 
            _current_state == LooperState::OVERDUBBING);
  }

  /**
   * @brief Resetea al estado IDLE.
   */
  void Reset() {
    TransitionTo(LooperState::IDLE);
  }

protected:
  /**
   * @brief Callback al entrar a un estado (override en clase derivada).
   */
  virtual void OnEnter(LooperState state) {
  }

  /**
   * @brief Callback al salir de un estado (override en clase derivada).
   */
  virtual void OnExit(LooperState state) {
  }

  /**
   * @brief Determina el próximo estado basado en el evento.
   */
  LooperState GetNextState(LooperState current, LooperEvent event) const {
    switch (current) {
      case LooperState::IDLE:
        if (event == LooperEvent::PRESS_REC) {
          return LooperState::RECORDING_INITIAL;
        }
        break;
        
      case LooperState::RECORDING_INITIAL:
        if (event == LooperEvent::RELEASE_REC) {
          return LooperState::PLAYING;
        } else if (event == LooperEvent::PRESS_STOP) {
          return LooperState::IDLE;
        }
        break;
        
      case LooperState::PLAYING:
        if (event == LooperEvent::PRESS_REC) {
          return LooperState::OVERDUBBING;
        } else if (event == LooperEvent::PRESS_PAUSE) {
          return LooperState::PAUSED;
        } else if (event == LooperEvent::PRESS_STOP || event == LooperEvent::CLEAR_LOOP) {
          return LooperState::IDLE;
        }
        break;
        
      case LooperState::OVERDUBBING:
        if (event == LooperEvent::RELEASE_REC) {
          return LooperState::PLAYING;
        } else if (event == LooperEvent::PRESS_STOP) {
          return LooperState::IDLE;
        }
        break;
        
      case LooperState::PAUSED:
        if (event == LooperEvent::PRESS_PLAY || event == LooperEvent::PRESS_PAUSE) {
          return LooperState::PLAYING;
        } else if (event == LooperEvent::PRESS_STOP || event == LooperEvent::CLEAR_LOOP) {
          return LooperState::IDLE;
        }
        break;
    }
    
    // Sin cambio de estado
    return current;
  }

private:
  LooperState _current_state;
  LooperState _previous_state;
};

/**
 * @brief Convierte estado a string (útil para debug).
 */
inline const char* StateToString(LooperState state) {
  switch (state) {
    case LooperState::IDLE: return "IDLE";
    case LooperState::RECORDING_INITIAL: return "RECORDING_INITIAL";
    case LooperState::PLAYING: return "PLAYING";
    case LooperState::OVERDUBBING: return "OVERDUBBING";
    case LooperState::PAUSED: return "PAUSED";
    default: return "UNKNOWN";
  }
}

/**
 * @brief Convierte evento a string (útil para debug).
 */
inline const char* EventToString(LooperEvent event) {
  switch (event) {
    case LooperEvent::PRESS_REC: return "PRESS_REC";
    case LooperEvent::RELEASE_REC: return "RELEASE_REC";
    case LooperEvent::PRESS_PLAY: return "PRESS_PLAY";
    case LooperEvent::PRESS_STOP: return "PRESS_STOP";
    case LooperEvent::PRESS_PAUSE: return "PRESS_PAUSE";
    case LooperEvent::LOOP_ENDED: return "LOOP_ENDED";
    case LooperEvent::CLEAR_LOOP: return "CLEAR_LOOP";
    default: return "UNKNOWN";
  }
}

} // namespace crearttech

#endif // SAMPLER_STATE_MACHINE_H
