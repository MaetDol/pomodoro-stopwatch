# Pomodoro Stopwatch – Agent Guidelines

## Project overview

- **Target hardware**: ESP32-S3 driving a 240×240 circular GC9A01A TFT for a Pomodoro timer/stopwatch.
- **Firmware structure (`src/`)**:
  - `pomodoro-stopwatch.ino`: Arduino entry point, ISR wiring, global state coordination.
  - `state_machine.cpp` · `states.cpp`: Mode transition logic and per-state behavior.
  - `render.cpp`: Drawing dial, arcs, labels, and other UI elements.
  - `input.cpp`: Rotary encoder and push button event handling.
  - `easing.cpp` · `utils.cpp`: Math/interpolation helpers and shared utilities.
  - `sleep.cpp`: Light-sleep entry and wake handling.
  - `pomodoro.h`: Shared constants, types, and extern declarations.

## Coding principles

1. **Prioritize readability**: Split long functions, use meaningful names, avoid clever macros or tricks.
2. **Respect existing style**: Two-space indentation, current brace placement, and consistent `const`/`constexpr` usage.
3. **Protect hardware flow**: Never place blocking calls inside ISRs or timing-critical paths; manage shared state carefully.
4. **Save resources**: Minimize dynamic allocation and favor `constexpr` or stack storage when possible.
5. **Refine before finishing**: Revisit the code before closing the task to deduplicate logic, tighten scopes, and refresh comments.

## Working checklist

1. Review the relevant files and understand the impact area before coding.
2. While implementing, keep functions focused, comments up to date, and code concise.
3. After feature completion, perform a quick cleanup/refactor pass.
4. Run available checks. When hardware access is unavailable, attempt alternatives such as `arduino-cli compile --fqbn <board>` or document why testing was skipped.
5. Document the changes and **always update this `AGENTS.md` at the end of your work**.

## Pull request rules

- **Title**: `[FEAT|FIX|REFACTOR|CHORE|DOCS|TEST] Description` written in Korean.
- **Body language**: Always write the PR body in Korean.
- **Body template** (keep mandatory sections, add more only if necessary):

  ```
  ## 개요
  - 주요 변화 요약 (불릿)

  ## 변경 내용
  - 파일/로직 단위로 구체적 설명

  ## 테스트
  - ✅ `command` - 결과 설명 (성공)
  - ⚠️/❌ `command` - 실패나 미실행 사유
  ```

- Even when tests are skipped or impossible, use `⚠️` or `❌` and explain why.
- Keep bullet points succinct so reviewers can parse them quickly.

---

> Every contributor must follow these rules and keep this document current after each change.
