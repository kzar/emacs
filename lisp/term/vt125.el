;; -*- no-byte-compile: t -*-

(defun terminal-init-vt125 ()
  "Terminal initialization function for vt125."
  (tty-run-terminal-initialization (selected-frame) "vt100"))

;;; arch-tag: 1d92d70f-dd55-4a1d-9088-e215a4883801
;;; vt125.el ends here
