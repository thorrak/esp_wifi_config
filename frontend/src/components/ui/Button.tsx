import { ComponentChildren } from 'preact';
import './Button.css';

interface ButtonProps {
  children: ComponentChildren;
  variant?: 'primary' | 'secondary' | 'danger' | 'ghost';
  size?: 'sm' | 'md';
  disabled?: boolean;
  loading?: boolean;
  onClick?: () => void;
  type?: 'button' | 'submit';
  class?: string;
}

export function Button({
  children,
  variant = 'primary',
  size = 'md',
  disabled,
  loading,
  onClick,
  type = 'button',
  class: className,
}: ButtonProps) {
  return (
    <button
      type={type}
      class={`btn btn-${variant} btn-${size} ${className || ''}`}
      disabled={disabled || loading}
      onClick={onClick}
    >
      {loading ? <span class="spinner" /> : children}
    </button>
  );
}
