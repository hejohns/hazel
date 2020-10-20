[@deriving sexp]
type t('a) = SpliceInfo.t(UHExp.t) => ('a, SpliceInfo.t(UHExp.t));
let return = (x, psi) => (x, psi);
let bind = (cmd, f, psi) => {
  let (a, psi') = cmd(psi);
  f(a, psi');
};

let exec = (cmd, psi, id_gen) => {
  let (a, psi) = cmd(psi);
  (a, psi, id_gen);
};

/* Have to add these functions to stub ppx_deriving.show for types that use this type */
let pp = (_, _, _) => ();
let show = _ => "SpliceGenMonad";
