[@deriving sexp]
type t =
  | Let
  | Case
  | TyAlias
  | Struct;

let is_Let = String.equal("let");
let is_Case = String.equal("case");
let is_TyAlias = String.equal("type");
let is_Struct = String.equal("module");

let mk = (text: string): option(t) =>
  if (text |> is_Let) {
    Some(Let);
  } else if (text |> is_Case) {
    Some(Case);
  } else if (text |> is_TyAlias) {
    Some(TyAlias);
  } else if (text |> is_Struct) {
    Some(Struct);
  } else {
    None;
  };

let to_string =
  fun
  | Let => "let"
  | Case => "case"
  | TyAlias => "type"
  | Struct => "module";
