#pragma once

namespace pbpfps::post_effect {

// HPL1 composites the old image with `amount` and the new image with
// `1 - amount` once per render. Convert that direct old-frame retention so its
// wall-time decay matches the reference render rate.
[[nodiscard]] bool CorrectImageTrailAmount(float stockAmount,
                                           float frameTimeSeconds,
                                           float referenceRateHz,
                                           float& correctedAmount) noexcept;

// HPL1's motion shader scales the displacement accumulated since the previous
// presentation. Normalize that scale to the displacement of one reference
// frame so blur length remains stable as presentation rate changes.
[[nodiscard]] bool CorrectMotionBlurScale(float stockScale,
                                          float frameTimeSeconds,
                                          float referenceRateHz,
                                          float& correctedScale) noexcept;

} // namespace pbpfps::post_effect
