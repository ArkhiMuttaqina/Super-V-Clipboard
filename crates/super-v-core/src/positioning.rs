//! Multi-monitor boundary clamping and pointer cursor overlay positioning math.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Point {
    pub x: i32,
    pub y: i32,
}

pub struct WindowPlacer;

impl WindowPlacer {
    /// Computes the clamped (X, Y) top-left coordinates for an overlay window
    /// of (win_w, win_h) anchored at pointer (pointer_x, pointer_y)
    /// within monitor boundaries (mon_x, mon_y, mon_w, mon_h) with padding.
    pub fn compute_position(
        pointer: Point,
        win_size: (i32, i32),
        monitor: Rect,
        padding: i32,
    ) -> Point {
        let (win_w, win_h) = win_size;

        // Initial desired placement: slightly offset from cursor
        let mut x = pointer.x + 8;
        let mut y = pointer.y + 8;

        let max_x = monitor.x + monitor.width - win_w - padding;
        let max_y = monitor.y + monitor.height - win_h - padding;
        let min_x = monitor.x + padding;
        let min_y = monitor.y + padding;

        // If window overflows right boundary, flip to left of pointer
        if x > max_x {
            x = (pointer.x - win_w - 8).max(min_x);
        }

        // If window overflows bottom boundary, flip above pointer
        if y > max_y {
            y = (pointer.y - win_h - 8).max(min_y);
        }

        Point {
            x: x.clamp(min_x, max_x.max(min_x)),
            y: y.clamp(min_y, max_y.max(min_y)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_normal_placement() {
        let monitor = Rect { x: 0, y: 0, width: 1920, height: 1080 };
        let pointer = Point { x: 500, y: 300 };
        let pos = WindowPlacer::compute_position(pointer, (420, 520), monitor, 12);
        assert_eq!(pos, Point { x: 508, y: 308 });
    }

    #[test]
    fn test_right_bottom_overflow_flip() {
        let monitor = Rect { x: 0, y: 0, width: 1920, height: 1080 };
        let pointer = Point { x: 1850, y: 1000 };
        let pos = WindowPlacer::compute_position(pointer, (420, 520), monitor, 12);
        // Flipped to left and above
        assert!(pos.x < 1850);
        assert!(pos.y < 1000);
        assert!(pos.x + 420 <= 1920 - 12);
        assert!(pos.y + 520 <= 1080 - 12);
    }
}
