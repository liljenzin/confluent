/**
 * @example PhoneNumbers.cc
 *
 * The program demonstrates how three-way-merges can be implemented by using
 * confluent::map as index type in a phone number dictionary.
 *
 * A three-way-merge is a git like merge of two or more branches that have been
 * updated in concurrent flows. The map is cloned in O(1) when the branch point
 * is created. Updating individual elements is O(log n).
 *
 * Let n be the total number of elements.
 * Let k be the number of updates after the branch point.
 * Merging branches using the example code is O(k*log(n/m)).
 *
 * Note that this performance is optimal for any sorted index structure as it
 * matches the cost for inserting elements into a sorted ephemeral structure.
 *
 * Copyright (c) 2017 Olle Liljenzin
 **/

#include <iostream>
#include <string>

#include "map.h"
#include "set.h"

typedef confluent::set<std::string> Keys;
typedef confluent::map<std::string, std::string> PhoneNumbers;

PhoneNumbers make_initial_numbers() {
  return {{"Emma", "(759) 534-1383"},      {"Olivia", "(124) 752-7453"},
          {"Ava", "(881) 352-1267"},       {"Sophia", "(213) 687-9617"},
          {"Mia", "(653) 724-0068"},       {"Amelia", "(181) 123-9026"},
          {"Charlotte", "(889) 254-0786"}, {"Harper", "(491) 307-5074"},
          {"Ella", "(608) 692-6507"},      {"Aira", "(860) 871-0985"}};
}

PhoneNumbers apply(PhoneNumbers old_master,
                   PhoneNumbers new_master,
                   PhoneNumbers branch,
                   std::string worker_name) {
  std::cout << "Apply changes by " << worker_name << std::endl;
  std::cout << "------------------------" << std::endl;

  PhoneNumbers branch_erased = old_master - branch;
  PhoneNumbers branch_inserted = branch - old_master;
  Keys keys_modified_by_branch =
      branch_erased.key_set() | branch_inserted.key_set();

  PhoneNumbers master_erased = old_master - new_master;
  PhoneNumbers master_inserted = new_master - old_master;
  Keys keys_modified_by_master =
      master_erased.key_set() | master_inserted.key_set();

  // Conflicting keys are keys whose records were modified in both branches.
  Keys conflicting_keys = keys_modified_by_branch & keys_modified_by_master;
  Keys erase_conflicts = conflicting_keys - branch_inserted.key_set();
  Keys insert_conflicts = conflicting_keys - branch_erased.key_set();
  Keys modify_conflicts = conflicting_keys - erase_conflicts - insert_conflicts;

  // Handle conflicts by printing them to the console.
  for (auto key : modify_conflicts)
    std::cout << key << "'s record not modified because of conflicts."
              << std::endl;
  for (auto key : erase_conflicts)
    std::cout << key << "'s record not erased because of conflicts."
              << std::endl;
  for (auto key : insert_conflicts)
    std::cout << key << "'s record not inserted because of conflicts."
              << std::endl;

  // Remove conflicts before applying changes.
  branch_erased -= conflicting_keys;
  branch_inserted -= conflicting_keys;

  // Apply changes on new master.
  new_master -= branch_erased;
  new_master |= branch_inserted;

  std::cout << "erased " << branch_erased.size() << " and inserted "
            << branch_inserted.size() << " entries" << std::endl
            << std::endl;

  return new_master;
}

PhoneNumbers worker1(PhoneNumbers numbers) {
  numbers.insert(std::make_pair("Evelyn", "(251) 546-9442"));
  numbers.erase("Mia");
  numbers.insert_or_assign(std::make_pair("Olivia", "(125) 546-4478"));
  return numbers;
}

PhoneNumbers worker2(PhoneNumbers numbers) {
  numbers.insert(std::make_pair("Madison", "(630) 446-8851"));
  numbers.erase("Mia");
  numbers.insert_or_assign(std::make_pair("Ava", "(226) 906-2721"));
  return numbers;
}

PhoneNumbers worker3(PhoneNumbers numbers) {
  numbers.insert(std::make_pair("Evelyn", "(949) 569-4371"));
  numbers.erase("Ella");
  numbers.insert_or_assign(std::make_pair("Ava", "(671) 925-1352"));
  numbers.insert(std::make_pair("Scarlett", "(402) 139-6590"));
  return numbers;
}

void print(const PhoneNumbers& numbers) {
  std::cout << "Phone numbers" << std::endl;
  std::cout << "-------------" << std::endl;
  for (auto entry : numbers)
    std::cout << entry.first << ": " << entry.second << std::endl;
  std::cout << std::endl;
}

int main() {
  // Load initial phone list.
  PhoneNumbers master = make_initial_numbers();

  // Create a tag representing a branch point.
  const PhoneNumbers tag = master;

  // Run workers that apply their changes in local branches.
  PhoneNumbers branch1 = worker1(tag);
  PhoneNumbers branch2 = worker2(tag);
  PhoneNumbers branch3 = worker3(tag);

  // Print phone numbers before applying changes.
  print(master);

  // Apply the local branches while reporting conflicts.
  master = apply(tag, master, branch1, "worker1");
  master = apply(tag, master, branch2, "worker2");
  master = apply(tag, master, branch3, "worker3");

  // Print phone numbers after changes were applied:
  print(master);
}
