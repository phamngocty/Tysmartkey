package com.example.control_esp

import android.content.Context
import android.graphics.*
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

/**
 * Custom View Vòng Bánh Xe Màu RGB 360° tương tác trực tiếp cho cảm biến R503.
 * Ánh xạ dải quang phổ RGB chuẩn xác sang 7 mã màu phần cứng (ColorIndex 1..7).
 */
class RgbColorWheelView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    // Danh mục 7 màu phần cứng của R503
    companion object {
        const val COLOR_RED    = 1
        const val COLOR_BLUE   = 2
        const val COLOR_PURPLE = 3
        const val COLOR_GREEN  = 4
        const val COLOR_YELLOW = 5
        const val COLOR_CYAN   = 6
        const val COLOR_WHITE  = 7

        data class R503ColorInfo(
            val index: Int,
            val name: String,
            val hex: Int,
            val angleDeg: Float // Góc đại diện trên bánh xe màu
        )

        val R503_COLORS = listOf(
            R503ColorInfo(COLOR_RED,    "Đỏ (Sport Red)",      0xFFFF2D55.toInt(),   0f),
            R503ColorInfo(COLOR_YELLOW, "Vàng (Solar Gold)",   0xFFFFCC00.toInt(),  60f),
            R503ColorInfo(COLOR_GREEN,  "Xanh Lá (Emerald)",   0xFF34C759.toInt(), 120f),
            R503ColorInfo(COLOR_CYAN,   "Xanh Ngọc (Cyan)",    0xFF00F5D4.toInt(), 180f),
            R503ColorInfo(COLOR_BLUE,   "Xanh Dương (Ocean)",  0xFF007AFF.toInt(), 240f),
            R503ColorInfo(COLOR_PURPLE, "Tím (Cyberpunk)",     0xFFAF52DE.toInt(), 300f),
            R503ColorInfo(COLOR_WHITE,  "Trắng (Pure White)",  0xFFFFFFFF.toInt(),   0f)
        )

        fun getColorInfo(index: Int): R503ColorInfo {
            return R503_COLORS.find { it.index == index } ?: R503_COLORS[4] // Mặc định Xanh Dương
        }
    }

    private val density = resources.displayMetrics.density
    private var currentColorIndex = COLOR_BLUE
    private var currentAngleDeg = 240f // Góc tương ứng Xanh dương
    private var isWhiteSelected = false

    // Paints
    private val wheelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
    }
    private val centerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }
    private val centerBorderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2.5f * density
        color = 0xFF475569.toInt()
    }
    private val thumbPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }
    private val thumbBorderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 3f * density
        color = Color.WHITE
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        textSize = 12f * density
        typeface = Typeface.DEFAULT_BOLD
        color = Color.WHITE
    }

    // Gradient mảng màu tròn 360 độ
    private val sweepColors = intArrayOf(
        0xFFFF2D55.toInt(), // Đỏ (0 deg)
        0xFFFFCC00.toInt(), // Vàng (60 deg)
        0xFF34C759.toInt(), // Xanh lá (120 deg)
        0xFF00F5D4.toInt(), // Xanh ngọc (180 deg)
        0xFF007AFF.toInt(), // Xanh dương (240 deg)
        0xFFAF52DE.toInt(), // Tím (300 deg)
        0xFFFF2D55.toInt()  // Đỏ (360 deg)
    )

    var onColorSelected: ((colorIndex: Int, colorHex: Int, colorName: String) -> Unit)? = null

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val defaultSize = (220 * density).toInt()
        val w = resolveSize(defaultSize, widthMeasureSpec)
        val h = resolveSize(defaultSize, heightMeasureSpec)
        val size = min(w, h)
        setMeasuredDimension(size, size)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        val cx = width / 2f
        val cy = height / 2f
        val strokeW = 28f * density
        wheelPaint.strokeWidth = strokeW

        val outerRadius = (min(width, height) / 2f) - (strokeW / 2f) - (8f * density)
        val centerRadius = outerRadius - (strokeW / 2f) - (14f * density)

        // 1. Vẽ dải màu tròn 360 độ (SweepGradient)
        wheelPaint.shader = SweepGradient(cx, cy, sweepColors, null)
        canvas.drawCircle(cx, cy, outerRadius, wheelPaint)

        // 2. Vẽ nút tâm chọn màu Trắng (Pure White)
        centerPaint.color = if (isWhiteSelected) Color.WHITE else 0xFF1E293B.toInt()
        canvas.drawCircle(cx, cy, centerRadius, centerPaint)
        centerBorderPaint.color = if (isWhiteSelected) 0xFFFFC107.toInt() else 0xFF475569.toInt()
        canvas.drawCircle(cx, cy, centerRadius, centerBorderPaint)

        // Chữ ở tâm hiển thị màu đang chọn
        val curInfo = getColorInfo(currentColorIndex)
        textPaint.color = if (isWhiteSelected) 0xFF0F172A.toInt() else curInfo.hex
        canvas.drawText(
            if (isWhiteSelected) "TRẮNG" else curInfo.name.substringBefore(" ("),
            cx, cy + (4f * density), textPaint
        )

        // 3. Vẽ con trỏ chọn màu (Thumb Glow)
        if (!isWhiteSelected) {
            val rad = Math.toRadians(currentAngleDeg.toDouble())
            val tx = cx + outerRadius * cos(rad).toFloat()
            val ty = cy + outerRadius * sin(rad).toFloat()

            thumbPaint.color = curInfo.hex
            canvas.drawCircle(tx, ty, 13f * density, thumbPaint)
            canvas.drawCircle(tx, ty, 13f * density, thumbBorderPaint)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.action) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                val cx = width / 2f
                val cy = height / 2f
                val dx = event.x - cx
                val dy = event.y - cy
                val dist = sqrt(dx * dx + dy * dy)

                val strokeW = 28f * density
                val outerRadius = (min(width, height) / 2f) - (strokeW / 2f) - (8f * density)
                val centerRadius = outerRadius - (strokeW / 2f) - (14f * density)

                if (dist <= centerRadius) {
                    // Chạm vào tâm -> Chọn màu Trắng
                    isWhiteSelected = true
                    currentColorIndex = COLOR_WHITE
                    val info = getColorInfo(COLOR_WHITE)
                    onColorSelected?.invoke(info.index, info.hex, info.name)
                    invalidate()
                    return true
                }

                // Chạm hoặc vuốt trên vòng bánh xe màu
                isWhiteSelected = false
                var angle = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble())).toFloat()
                if (angle < 0) angle += 360f

                currentAngleDeg = angle
                // Ánh xạ góc sang 6 màu vòng ngoài
                currentColorIndex = mapAngleToColorIndex(angle)
                val info = getColorInfo(currentColorIndex)
                onColorSelected?.invoke(info.index, info.hex, info.name)

                invalidate()
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    /**
     * Ánh xạ góc quay (0..360) sang 6 màu chính:
     * 0° / 360° : Đỏ
     * 60°       : Vàng
     * 120°      : Xanh Lá
     * 180°      : Xanh Ngọc
     * 240°      : Xanh Dương
     * 300°      : Tím
     */
    private fun mapAngleToColorIndex(deg: Float): Int {
        return when {
            deg in 30f..89f   -> COLOR_YELLOW
            deg in 90f..149f  -> COLOR_GREEN
            deg in 150f..209f -> COLOR_CYAN
            deg in 210f..269f -> COLOR_BLUE
            deg in 270f..329f -> COLOR_PURPLE
            else              -> COLOR_RED // 330..360 hoặc 0..29
        }
    }

    /**
     * Cập nhật màu từ bên ngoài (ví dụ nạp từ NVS hoặc click Preset)
     */
    fun setColorIndex(index: Int) {
        val clampedIndex = if (index in 1..7) index else COLOR_BLUE
        currentColorIndex = clampedIndex
        isWhiteSelected = (clampedIndex == COLOR_WHITE)
        val info = getColorInfo(clampedIndex)
        currentAngleDeg = info.angleDeg
        invalidate()
    }

    fun getCurrentColorIndex(): Int = currentColorIndex
}
